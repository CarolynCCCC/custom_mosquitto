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

#include "mosquitto_broker_internal.h"
#include "mosquitto/mqtt_protocol.h"
#include "packet_mosq.h"
#include "property_mosq.h"
#include "sys_tree.h"
#include "util_mosq.h"


int send__suback(struct mosquitto *context, uint16_t mid, uint32_t payloadlen, const void *payload)
{
	struct mosquitto__packet *packet = NULL;
	int rc;
	mosquitto_property *properties = NULL;
	uint32_t remaining_length;

	log__printf(NULL, MOSQ_LOG_DEBUG, "Sending SUBACK to %s", context->id);

	remaining_length = 2+payloadlen;
	if(context->protocol == mosq_p_mqtt5){
		remaining_length += mosquitto_property_get_remaining_length(properties);
	}
	rc = packet__alloc(&packet, CMD_SUBACK, remaining_length);
	if(rc){
		return rc;
	}
	packet__write_uint16(packet, mid);

	if(context->protocol == mosq_p_mqtt5){
		/* We don't use Reason String or User Property yet. */
		property__write_all(packet, properties, true);
	}

	if(payloadlen){
		packet__write_bytes(packet, payload, payloadlen);
	}

	metrics__int_inc(mosq_counter_mqtt_suback_sent, 1);
	return packet__queue(context, packet);
}
