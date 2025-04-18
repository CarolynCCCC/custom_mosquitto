/*
Copyright (c) 2009-2021 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License 2.0
and Eclipse Distribution License v1.0 which accompany this distribution.

The Eclipse Public License is available at
   https://www.eclipse.org/legal/epl-2.0/
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.

SPDX-License-Identifier: EPL-2.0 OR BSD-3-Clause

Contributors:
   Roger Light - initial implementation and documentation.
*/

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "mosquitto_broker_internal.h"
#include "mosquitto/mqtt_protocol.h"
#include "packet_mosq.h"
#include "property_mosq.h"
#include "sys_tree.h"

extern void gen_active_user_list(struct mosquitto_db *db);

int handle__subscribe(struct mosquitto *context)
{
	int rc = 0;
	int rc2;
	uint16_t mid;
	uint8_t qos;
	uint8_t retain_handling = 0;
	uint8_t *payload = NULL, *tmp_payload;
	uint32_t payloadlen = 0;
	size_t len;
	uint16_t slen;
	char *sub_mount;
	mosquitto_property *properties = NULL;
	bool allowed;
	struct mosquitto_subscription sub;
	uint32_t subscription_identifier = 0;
	char active_users_sub[128] = "";

	if(!context) return MOSQ_ERR_INVAL;

	if(context->state != mosq_cs_active){
		return MOSQ_ERR_PROTOCOL;
	}
	if(context->in_packet.command != (CMD_SUBSCRIBE|2)){
		return MOSQ_ERR_MALFORMED_PACKET;
	}

	log__printf(NULL, MOSQ_LOG_DEBUG, "Received SUBSCRIBE from %s", context->id);

	if(context->protocol != mosq_p_mqtt31){
		if((context->in_packet.command&0x0F) != 0x02){
			return MOSQ_ERR_MALFORMED_PACKET;
		}
	}
	if(packet__read_uint16(&context->in_packet, &mid)) return MOSQ_ERR_MALFORMED_PACKET;
	if(mid == 0) return MOSQ_ERR_MALFORMED_PACKET;

	if(context->protocol == mosq_p_mqtt5){
		rc = property__read_all(CMD_SUBSCRIBE, &context->in_packet, &properties);
		if(rc){
			/* FIXME - it would be better if property__read_all() returned
			 * MOSQ_ERR_MALFORMED_PACKET, but this is would change the library
			 * return codes so needs doc changes as well. */
			if(rc == MOSQ_ERR_PROTOCOL){
				return MOSQ_ERR_MALFORMED_PACKET;
			}else{
				return rc;
			}
		}

		if(mosquitto_property_read_varint(properties, MQTT_PROP_SUBSCRIPTION_IDENTIFIER,
					&subscription_identifier, false)){

			/* If the identifier was force set to 0, this is an error */
			if(subscription_identifier == 0){
				mosquitto_property_free_all(&properties);
				return MOSQ_ERR_MALFORMED_PACKET;
			}
		}

		mosquitto_property_free_all(&properties);
		/* Note - User Property not handled */
	}

	while(context->in_packet.pos < context->in_packet.remaining_length){
		memset(&sub, 0, sizeof(sub));
		sub.identifier = subscription_identifier;
		sub.properties = properties;
		if(packet__read_string(&context->in_packet, &sub.topic_filter, &slen)){
			mosquitto_FREE(payload);
			return MOSQ_ERR_MALFORMED_PACKET;
		}

		if(sub.topic_filter){
			strncpy(active_users_sub, sub.topic_filter, 127);
			if(!slen){
				log__printf(NULL, MOSQ_LOG_INFO,
						"Empty subscription string from %s, disconnecting.",
						context->address);
				mosquitto_FREE(sub.topic_filter);
				mosquitto_FREE(payload);
				return MOSQ_ERR_MALFORMED_PACKET;
			}
			if(mosquitto_sub_topic_check(sub.topic_filter)){
				log__printf(NULL, MOSQ_LOG_INFO,
						"Invalid subscription string from %s, disconnecting.",
						context->address);
				mosquitto_FREE(sub.topic_filter);
				mosquitto_FREE(payload);
				return MOSQ_ERR_MALFORMED_PACKET;
			}

			if(packet__read_byte(&context->in_packet, &sub.options)){
				mosquitto_FREE(sub.topic_filter);
				mosquitto_FREE(payload);
				return MOSQ_ERR_MALFORMED_PACKET;
			}
			if(sub.options & MQTT_SUB_OPT_NO_LOCAL && !strncmp(sub.topic_filter, "$share/", strlen("$share/"))){
				mosquitto_FREE(sub.topic_filter);
				mosquitto_FREE(payload);
				return MOSQ_ERR_PROTOCOL;
			}

			if(context->protocol == mosq_p_mqtt31 || context->protocol == mosq_p_mqtt311){
				qos = sub.options;
				sub.options = 0;
				if(context->is_bridge){
					sub.options |= MQTT_SUB_OPT_RETAIN_AS_PUBLISHED | MQTT_SUB_OPT_NO_LOCAL;
				}
			}else{
				qos = sub.options & 0x03;
				sub.options &= 0xFC;

				if(MQTT_SUB_OPT_GET_NO_LOCAL(sub.options) && !strncmp(sub.topic_filter, "$share/", 7)){
					mosquitto_FREE(sub.topic_filter);
					mosquitto_FREE(payload);
					return MOSQ_ERR_PROTOCOL;
				}
				retain_handling = MQTT_SUB_OPT_GET_SEND_RETAIN(sub.options);
				if(retain_handling == 0x30 || (sub.options & 0xC0) != 0){
					mosquitto_FREE(sub.topic_filter);
					mosquitto_FREE(payload);
					return MOSQ_ERR_MALFORMED_PACKET;
				}
			}
			if(qos > 2){
				log__printf(NULL, MOSQ_LOG_INFO,
						"Invalid QoS in subscription command from %s, disconnecting.",
						context->address);
				mosquitto_FREE(sub.topic_filter);
				mosquitto_FREE(payload);
				return MOSQ_ERR_MALFORMED_PACKET;
			}
			if(qos > context->max_qos){
				qos = context->max_qos;
			}
			sub.options |= qos;


			if(context->listener && context->listener->mount_point){
				len = strlen(context->listener->mount_point) + slen + 1;
				sub_mount = mosquitto_malloc(len+1);
				if(!sub_mount){
					mosquitto_FREE(sub.topic_filter);
					mosquitto_FREE(payload);
					return MOSQ_ERR_NOMEM;
				}
				snprintf(sub_mount, len, "%s%s", context->listener->mount_point, sub.topic_filter);
				sub_mount[len] = '\0';

				mosquitto_FREE(sub.topic_filter);
				sub.topic_filter = sub_mount;
			}

			/* Check if the topic is a broadcast topic first */
			bool is_broadcast = false;
			if(db.config->broadcast_topic){
				mosquitto_topic_matches_sub(sub.topic_filter, db.config->broadcast_topic, &is_broadcast);
			}

			/* Handle per-user mount point if enabled and not an admin user */
			if(db.config->mount_point_per_user && context->username && strncmp(context->username, "admin", 5) != 0 && !is_broadcast){
				len = strlen(context->username) + strlen(sub.topic_filter) + 2; /* +2 for '/' and null terminator */
				sub_mount = mosquitto_malloc(len);
				if(!sub_mount){
					mosquitto_FREE(sub.topic_filter);
					mosquitto_FREE(payload);
					return MOSQ_ERR_NOMEM;
				}
				snprintf(sub_mount, len, "%s/%s", context->username, sub.topic_filter);
				sub_mount[len-1] = '\0';

				mosquitto_FREE(sub.topic_filter);
				sub.topic_filter = sub_mount;
			}

			allowed = true;
			rc2 = mosquitto_acl_check(context, sub.topic_filter, 0, NULL, qos, false, MOSQ_ACL_SUBSCRIBE);
			switch(rc2){
				case MOSQ_ERR_SUCCESS:
					break;
				case MOSQ_ERR_ACL_DENIED:
					allowed = false;
					if(context->protocol == mosq_p_mqtt5){
						qos = MQTT_RC_NOT_AUTHORIZED;
					}else if(context->protocol == mosq_p_mqtt311){
						qos = 0x80;
					}
					break;
				default:
					mosquitto_FREE(sub.topic_filter);
					return rc2;
			}
			if(qos > 127){
				log__printf(NULL, MOSQ_LOG_DEBUG, "\t%s (denied)", sub.topic_filter);
			}else{
				log__printf(NULL, MOSQ_LOG_DEBUG, "\t%s (QoS %d)", sub.topic_filter, qos);
			}

			if(allowed){
				rc2 = plugin__handle_subscribe(context, &sub);
				if(rc2){
					mosquitto_FREE(sub.topic_filter);
					return rc2;
				}

				rc2 = sub__add(context, &sub);
				if(rc2 > 0){
					mosquitto_FREE(sub.topic_filter);
					return rc2;
				}
				if(context->protocol == mosq_p_mqtt311 || context->protocol == mosq_p_mqtt31){
					if(rc2 == MOSQ_ERR_SUCCESS || rc2 == MOSQ_ERR_SUB_EXISTS){
						if(retain__queue(context, &sub)){
							mosquitto_FREE(sub.topic_filter);
							return rc;
						}
					}
				}else{
					if((retain_handling == MQTT_SUB_OPT_SEND_RETAIN_ALWAYS)
							|| (rc2 == MOSQ_ERR_SUCCESS && retain_handling == MQTT_SUB_OPT_SEND_RETAIN_NEW)){

						if(retain__queue(context, &sub)){
							mosquitto_FREE(sub.topic_filter);
							return rc;
						}
					}
				}
				log__printf(NULL, MOSQ_LOG_SUBSCRIBE, "%s %d %s", context->id, qos, sub.topic_filter);

				plugin_persist__handle_subscription_add(context, &sub);

				/* Update user metrics for subscription */
				if(context->username && db.config->user_stats){
					user_metrics__update_subscription(context, sub.topic_filter, true);
				}
			}
			mosquitto_FREE(sub.topic_filter);

			tmp_payload = mosquitto_realloc(payload, payloadlen + 1);
			if(tmp_payload){
				payload = tmp_payload;
				payload[payloadlen] = qos;
				payloadlen++;
			}else{
				mosquitto_FREE(payload);

				return MOSQ_ERR_NOMEM;
			}
		}
	}

	if(context->protocol != mosq_p_mqtt31){
		if(payloadlen == 0){
			/* No subscriptions specified, protocol error. */
			return MOSQ_ERR_MALFORMED_PACKET;
		}
	}
	if(send__suback(context, mid, payloadlen, payload)) rc = 1;
	mosquitto_FREE(payload);

	printf("active_users_sub: %s\n", active_users_sub);
	if(strcmp(active_users_sub, "$SYS/broker/active_users") == 0){
		gen_active_user_list(&db);
	}

#ifdef WITH_PERSISTENCE
	db.persistence_changes++;
#endif

	if(context->out_packet == NULL){
		rc = db__message_write_queued_out(context);
		if(rc) return rc;
		rc = db__message_write_inflight_out_latest(context);
		if(rc) return rc;
	}

	return rc;
}
