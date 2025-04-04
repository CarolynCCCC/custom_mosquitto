/*
Copyright (c) 2010-2021 Roger Light <roger@atchoo.org>

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

#ifdef WITH_PERSISTENCE

#ifndef WIN32
#include <arpa/inet.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "mosquitto/mqtt_protocol.h"
#include "mosquitto_broker_internal.h"
#include "persist.h"
#include "packet_mosq.h"
#include "property_common.h"
#include "property_mosq.h"
#include "util_mosq.h"

int persist__chunk_cfg_write_v6(FILE *db_fptr, struct PF_cfg *chunk)
{
	struct PF_header header;

	header.chunk = htonl(DB_CHUNK_CFG);
	header.length = htonl(sizeof(struct PF_cfg));
	write_e(db_fptr, &header, sizeof(struct PF_header));
	write_e(db_fptr, chunk, sizeof(struct PF_cfg));

	return MOSQ_ERR_SUCCESS;
error:
	log__printf(NULL, MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	return 1;
}


int persist__chunk_client_write_v6(FILE *db_fptr, struct P_client *chunk)
{
	struct PF_header header;
	uint16_t id_len = chunk->F.id_len;
	uint16_t username_len = chunk->F.username_len;

	chunk->F.session_expiry_interval = htonl(chunk->F.session_expiry_interval);
	chunk->F.last_mid = htons(chunk->F.last_mid);
	chunk->F.id_len = htons(chunk->F.id_len);
	chunk->F.username_len = htons(chunk->F.username_len);
	chunk->F.listener_port = htons(chunk->F.listener_port);

	header.chunk = htonl(DB_CHUNK_CLIENT);
	header.length = htonl((uint32_t)sizeof(struct PF_client)+id_len+username_len);

	write_e(db_fptr, &header, sizeof(struct PF_header));
	write_e(db_fptr, &chunk->F, sizeof(struct PF_client));

	write_e(db_fptr, chunk->clientid, id_len);
	if(username_len > 0){
		write_e(db_fptr, chunk->username, username_len);
	}

	return MOSQ_ERR_SUCCESS;
error:
	log__printf(NULL, MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	return 1;
}


int persist__chunk_client_msg_write_v6(FILE *db_fptr, struct P_client_msg *chunk)
{
	struct PF_header header;
	struct mosquitto__packet *prop_packet = NULL;
	uint16_t id_len = chunk->F.id_len;
	uint32_t proplen = 0;
	int rc;
	mosquitto_property subscription_id_prop =
			{.next = NULL, .identifier = MQTT_PROP_SUBSCRIPTION_IDENTIFIER, .client_generated = true, .property_type = MQTT_PROP_TYPE_VARINT};

	if(chunk->subscription_identifier){
		subscription_id_prop.value.varint = chunk->subscription_identifier;
		proplen += mosquitto_property_get_remaining_length(&subscription_id_prop);
	}

	chunk->F.mid = htons(chunk->F.mid);
	chunk->F.id_len = htons(chunk->F.id_len);

	header.chunk = htonl(DB_CHUNK_CLIENT_MSG);
	header.length = htonl((uint32_t)sizeof(struct PF_client_msg) + id_len + proplen);

	write_e(db_fptr, &header, sizeof(struct PF_header));
	write_e(db_fptr, &chunk->F, sizeof(struct PF_client_msg));
	write_e(db_fptr, chunk->clientid, id_len);
	if(chunk->subscription_identifier){
		if(proplen > 0){
			prop_packet = mosquitto_calloc(1, sizeof(struct mosquitto__packet)+proplen);
			if(prop_packet == NULL){
				return MOSQ_ERR_NOMEM;
			}
			prop_packet->remaining_length = proplen;
			prop_packet->packet_length = proplen;
			rc = property__write_all(prop_packet, &subscription_id_prop, true);
			if(rc){
				mosquitto_FREE(prop_packet);
				return rc;
			}

			write_e(db_fptr, prop_packet->payload, proplen);
			mosquitto_FREE(prop_packet);
		}
	}

	return MOSQ_ERR_SUCCESS;
error:
	log__printf(NULL, MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	mosquitto_FREE(prop_packet);
	return 1;
}


int persist__chunk_message_store_write_v6(FILE *db_fptr, struct P_base_msg *chunk)
{
	struct PF_header header;
	uint32_t payloadlen = chunk->F.payloadlen;
	uint16_t source_id_len = chunk->F.source_id_len;
	uint16_t source_username_len = chunk->F.source_username_len;
	uint16_t topic_len = chunk->F.topic_len;
	uint32_t proplen = 0;
	int rc;

	if(chunk->properties){
		proplen += mosquitto_property_get_remaining_length(chunk->properties);
	}

	chunk->F.payloadlen = htonl(chunk->F.payloadlen);
	chunk->F.source_mid = htons(chunk->F.source_mid);
	chunk->F.source_id_len = htons(chunk->F.source_id_len);
	chunk->F.source_username_len = htons(chunk->F.source_username_len);
	chunk->F.topic_len = htons(chunk->F.topic_len);
	chunk->F.source_port = htons(chunk->F.source_port);

	header.chunk = htonl(DB_CHUNK_BASE_MSG);
	header.length = htonl((uint32_t)sizeof(struct PF_base_msg) +
			topic_len + payloadlen +
			source_id_len + source_username_len + proplen);

	write_e(db_fptr, &header, sizeof(struct PF_header));
	write_e(db_fptr, &chunk->F, sizeof(struct PF_base_msg));
	if(source_id_len){
		write_e(db_fptr, chunk->source.id, source_id_len);
	}
	if(source_username_len){
		write_e(db_fptr, chunk->source.username, source_username_len);
	}
	write_e(db_fptr, chunk->topic, topic_len);
	if(payloadlen){
		write_e(db_fptr, chunk->payload, (unsigned int)payloadlen);
	}
	if(chunk->properties){
		if(proplen > 0){
			struct mosquitto__packet *prop_packet = mosquitto_calloc(1, sizeof(struct mosquitto__packet)+proplen);
			if(prop_packet == NULL){
				return MOSQ_ERR_NOMEM;
			}
			prop_packet->remaining_length = proplen;
			prop_packet->packet_length = proplen;
			rc = property__write_all(prop_packet, chunk->properties, true);
			if(rc){
				mosquitto_FREE(prop_packet);
				return rc;
			}

			if(fwrite(prop_packet->payload, 1, proplen, db_fptr) != proplen){
				mosquitto_FREE(prop_packet);
				goto error;
			}
			mosquitto_FREE(prop_packet);
		}
	}

	return MOSQ_ERR_SUCCESS;
error:
	log__printf(NULL, MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	return 1;
}


int persist__chunk_retain_write_v6(FILE *db_fptr, struct P_retain *chunk)
{
	struct PF_header header;

	header.chunk = htonl(DB_CHUNK_RETAIN);
	header.length = htonl((uint32_t)sizeof(struct PF_retain));

	write_e(db_fptr, &header, sizeof(struct PF_header));
	write_e(db_fptr, &chunk->F, sizeof(struct PF_retain));

	return MOSQ_ERR_SUCCESS;
error:
	log__printf(NULL, MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	return 1;
}


int persist__chunk_sub_write_v6(FILE *db_fptr, struct P_sub *chunk)
{
	struct PF_header header;
	uint16_t id_len = chunk->F.id_len;
	uint16_t topic_len = chunk->F.topic_len;

	chunk->F.identifier = htonl(chunk->F.identifier);
	chunk->F.id_len = htons(chunk->F.id_len);
	chunk->F.topic_len = htons(chunk->F.topic_len);

	header.chunk = htonl(DB_CHUNK_SUB);
	header.length = htonl((uint32_t)sizeof(struct PF_sub) +
			id_len + topic_len);

	write_e(db_fptr, &header, sizeof(struct PF_header));
	write_e(db_fptr, &chunk->F, sizeof(struct PF_sub));
	write_e(db_fptr, chunk->clientid, id_len);
	write_e(db_fptr, chunk->topic, topic_len);

	return MOSQ_ERR_SUCCESS;
error:
	log__printf(NULL, MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	return 1;
}
#endif
