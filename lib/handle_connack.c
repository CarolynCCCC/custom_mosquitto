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

#include <assert.h>

#include "callbacks.h"
#include "logging_mosq.h"
#include "messages_mosq.h"
#include "mosquitto/mqtt_protocol.h"
#include "net_mosq.h"
#include "packet_mosq.h"
#include "property_mosq.h"
#include "read_handle.h"
#include "util_mosq.h"

int handle__connack(struct mosquitto *mosq)
{
	uint8_t connect_flags;
	uint8_t reason_code;
	int rc;
	mosquitto_property *properties = NULL;
	char *clientid = NULL;
	enum mosquitto_client_state state;

	assert(mosq);
	state = mosquitto__get_state(mosq);
	if(state != mosq_cs_new && state != mosq_cs_connected){
		log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s received duplicate CONNACK", mosq->id);
		return MOSQ_ERR_PROTOCOL;
	}

	if(mosq->in_packet.command != CMD_CONNACK
			|| ((mosq->protocol == 3 || mosq->protocol == 4) && mosq->in_packet.remaining_length != 2)){

		return MOSQ_ERR_MALFORMED_PACKET;
	}

	rc = packet__read_byte(&mosq->in_packet, &connect_flags);
	if(rc) return rc;
	if((mosq->protocol == mosq_p_mqtt311 || mosq->protocol == mosq_p_mqtt5) && (connect_flags & 0xFE)){
		log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s received CONNACK with invalid connect flags (%d)", mosq->id, connect_flags);
		return MOSQ_ERR_PROTOCOL;
	}
	if(mosq->clean_start && connect_flags){
		log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s received CONNACK with session present when clean start was set", mosq->id);
		return MOSQ_ERR_PROTOCOL;
	}

	rc = packet__read_byte(&mosq->in_packet, &reason_code);
	if(rc) return rc;

	if(mosq->protocol == mosq_p_mqtt5){
		rc = property__read_all(CMD_CONNACK, &mosq->in_packet, &properties);

		if(rc == MOSQ_ERR_PROTOCOL && reason_code == CONNACK_REFUSED_PROTOCOL_VERSION){
			/* This could occur because we are connecting to a v3.x broker and
			 * it has replied with "unacceptable protocol version", but with a
			 * v3 CONNACK. */

			log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s received CONNACK (%d)", mosq->id, reason_code);
			callback__on_connect(mosq, MQTT_RC_UNSUPPORTED_PROTOCOL_VERSION, connect_flags, NULL);
			return rc;
		}else if(rc){
			return rc;
		}
	}

	mosquitto_property_read_string(properties, MQTT_PROP_ASSIGNED_CLIENT_IDENTIFIER, &clientid, false);
	if(clientid){
		if(mosq->id){
			/* We've been sent a client identifier but already have one. This
			 * shouldn't happen. */
			mosquitto_FREE(clientid);
			mosquitto_property_free_all(&properties);
			return MOSQ_ERR_PROTOCOL;
		}else{
			mosq->id = clientid;
			clientid = NULL;
		}
	}

	mosquitto_property_read_byte(properties, MQTT_PROP_RETAIN_AVAILABLE, &mosq->retain_available, false);
	mosquitto_property_read_byte(properties, MQTT_PROP_MAXIMUM_QOS, &mosq->max_qos, false);
	mosquitto_property_read_int16(properties, MQTT_PROP_RECEIVE_MAXIMUM, &mosq->msgs_out.inflight_maximum, false);
	mosquitto_property_read_int16(properties, MQTT_PROP_SERVER_KEEP_ALIVE, &mosq->keepalive, false);
	mosquitto_property_read_int32(properties, MQTT_PROP_MAXIMUM_PACKET_SIZE, &mosq->maximum_packet_size, false);

	mosq->msgs_out.inflight_quota = mosq->msgs_out.inflight_maximum;
	message__reconnect_reset(mosq, true);

	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s received CONNACK (%d)", mosq->id, reason_code);
	if(reason_code == MQTT_RC_SUCCESS){
		mosq->reconnects = 0;
	}
	callback__on_connect(mosq, reason_code, connect_flags, properties);
	mosquitto_property_free_all(&properties);

	switch(reason_code){
		case 0:
			pthread_mutex_lock(&mosq->state_mutex);
			if(mosq->state != mosq_cs_disconnecting){
				mosq->state = mosq_cs_active;
			}
			pthread_mutex_unlock(&mosq->state_mutex);
			message__retry_check(mosq);
			return MOSQ_ERR_SUCCESS;
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
			return MOSQ_ERR_CONN_REFUSED;
		default:
			return MOSQ_ERR_PROTOCOL;
	}
}

