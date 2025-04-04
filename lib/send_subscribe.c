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
#include <string.h>

#ifdef WITH_BROKER
#  include "mosquitto_broker_internal.h"
#  include "sys_tree.h"
#endif

#include "mosquitto.h"
#include "mosquitto_internal.h"
#include "logging_mosq.h"
#include "mosquitto/mqtt_protocol.h"
#include "packet_mosq.h"
#include "property_mosq.h"
#include "send_mosq.h"
#include "util_mosq.h"


int send__subscribe(struct mosquitto *mosq, int *mid, int topic_count, char *const *const topic, int topic_qos, const mosquitto_property *properties)
{
	struct mosquitto__packet *packet = NULL;
	uint32_t packetlen;
	uint16_t local_mid;
	int rc;
	int i;
	size_t tlen;

	assert(mosq);
	assert(topic);

	packetlen = 2;
	if(mosq->protocol == mosq_p_mqtt5){
		packetlen += mosquitto_property_get_remaining_length(properties);
	}
	for(i=0; i<topic_count; i++){
		tlen = strlen(topic[i]);
		if(tlen > UINT16_MAX){
			return MOSQ_ERR_INVAL;
		}
		packetlen += 2U+(uint16_t)tlen + 1U;
	}

	rc = packet__alloc(&packet, CMD_SUBSCRIBE | 2, packetlen);
	if(rc){
		mosquitto_FREE(packet);
		return rc;
	}

	/* Variable header */
	local_mid = mosquitto__mid_generate(mosq);
	if(mid) *mid = (int)local_mid;
	packet__write_uint16(packet, local_mid);

	if(mosq->protocol == mosq_p_mqtt5){
		property__write_all(packet, properties, true);
	}

	/* Payload */
	for(i=0; i<topic_count; i++){
		packet__write_string(packet, topic[i], (uint16_t)strlen(topic[i]));
		packet__write_byte(packet, (uint8_t)topic_qos);
	}

#ifdef WITH_BROKER
# ifdef WITH_BRIDGE
	log__printf(mosq, MOSQ_LOG_DEBUG, "Bridge %s sending SUBSCRIBE (Mid: %d, Topic: %s, QoS: %d, Options: 0x%02x)", SAFE_PRINT(mosq->id), local_mid, topic[0], topic_qos&0x03, topic_qos&0xFC);
# endif
#else
	for(i=0; i<topic_count; i++){
		log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s sending SUBSCRIBE (Mid: %d, Topic: %s, QoS: %d, Options: 0x%02x)", SAFE_PRINT(mosq->id), local_mid, topic[i], topic_qos&0x03, topic_qos&0xFC);
	}
#endif

#ifdef WITH_BROKER
	metrics__int_inc(mosq_counter_mqtt_subscribe_sent, 1);
#endif
	return packet__queue(mosq, packet);
}
