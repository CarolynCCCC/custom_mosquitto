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
#include <stdio.h>
#include <string.h>

#ifdef WITH_BROKER
#  include "mosquitto_broker_internal.h"
#  include "sys_tree.h"
#endif

#include "mosquitto.h"
#include "mosquitto_internal.h"
#include "logging_mosq.h"
#include "mosquitto/mqtt_protocol.h"
#include "net_mosq.h"
#include "packet_mosq.h"
#include "property_mosq.h"
#include "send_mosq.h"
#include "util_mosq.h"

int send__pingreq(struct mosquitto *mosq)
{
	int rc;
	assert(mosq);
#ifdef WITH_BROKER
	log__printf(NULL, MOSQ_LOG_DEBUG, "Sending PINGREQ to %s", SAFE_PRINT(mosq->id));
#else
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s sending PINGREQ", SAFE_PRINT(mosq->id));
#endif
	rc = send__simple_command(mosq, CMD_PINGREQ);
	if(rc == MOSQ_ERR_SUCCESS){
		mosq->ping_t = mosquitto_time();
#ifdef WITH_BROKER
		metrics__int_inc(mosq_counter_mqtt_pingreq_sent, 1);
#endif
	}
	return rc;
}

int send__pingresp(struct mosquitto *mosq)
{
#ifdef WITH_BROKER
	log__printf(NULL, MOSQ_LOG_DEBUG, "Sending PINGRESP to %s", SAFE_PRINT(mosq->id));
	metrics__int_inc(mosq_counter_mqtt_pingresp_sent, 1);
#else
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s sending PINGRESP", SAFE_PRINT(mosq->id));
#endif
	return send__simple_command(mosq, CMD_PINGRESP);
}

int send__puback(struct mosquitto *mosq, uint16_t mid, uint8_t reason_code, const mosquitto_property *properties)
{
#ifdef WITH_BROKER
	log__printf(NULL, MOSQ_LOG_DEBUG, "Sending PUBACK to %s (m%d, rc%d)", SAFE_PRINT(mosq->id), mid, reason_code);
	metrics__int_inc(mosq_counter_mqtt_puback_sent, 1);
#else
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s sending PUBACK (m%d, rc%d)", SAFE_PRINT(mosq->id), mid, reason_code);
#endif
	util__increment_receive_quota(mosq);
	/* We don't use Reason String or User Property yet. */
	return send__command_with_mid(mosq, CMD_PUBACK, mid, false, reason_code, properties);
}

int send__pubcomp(struct mosquitto *mosq, uint16_t mid, const mosquitto_property *properties)
{
#ifdef WITH_BROKER
	log__printf(NULL, MOSQ_LOG_DEBUG, "Sending PUBCOMP to %s (m%d)", SAFE_PRINT(mosq->id), mid);
	metrics__int_inc(mosq_counter_mqtt_pubcomp_sent, 1);
#else
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s sending PUBCOMP (m%d)", SAFE_PRINT(mosq->id), mid);
#endif
	util__increment_receive_quota(mosq);
	/* We don't use Reason String or User Property yet. */
	return send__command_with_mid(mosq, CMD_PUBCOMP, mid, false, 0, properties);
}


int send__pubrec(struct mosquitto *mosq, uint16_t mid, uint8_t reason_code, const mosquitto_property *properties)
{
#ifdef WITH_BROKER
	log__printf(NULL, MOSQ_LOG_DEBUG, "Sending PUBREC to %s (m%d, rc%d)", SAFE_PRINT(mosq->id), mid, reason_code);
	metrics__int_inc(mosq_counter_mqtt_pubrec_sent, 1);
#else
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s sending PUBREC (m%d, rc%d)", SAFE_PRINT(mosq->id), mid, reason_code);
#endif
	if(reason_code >= 0x80 && mosq->protocol == mosq_p_mqtt5){
		util__increment_receive_quota(mosq);
	}
	/* We don't use Reason String or User Property yet. */
	return send__command_with_mid(mosq, CMD_PUBREC, mid, false, reason_code, properties);
}

int send__pubrel(struct mosquitto *mosq, uint16_t mid, const mosquitto_property *properties)
{
#ifdef WITH_BROKER
	log__printf(NULL, MOSQ_LOG_DEBUG, "Sending PUBREL to %s (m%d)", SAFE_PRINT(mosq->id), mid);
	metrics__int_inc(mosq_counter_mqtt_pubrel_sent, 1);
#else
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s sending PUBREL (m%d)", SAFE_PRINT(mosq->id), mid);
#endif
	/* We don't use Reason String or User Property yet. */
	return send__command_with_mid(mosq, CMD_PUBREL|2, mid, false, 0, properties);
}

/* For PUBACK, PUBCOMP, PUBREC, and PUBREL */
int send__command_with_mid(struct mosquitto *mosq, uint8_t command, uint16_t mid, bool dup, uint8_t reason_code, const mosquitto_property *properties)
{
	struct mosquitto__packet *packet = NULL;
	int rc;
	uint32_t remaining_length;

	assert(mosq);

	if(dup){
		command |= 8;
	}
	remaining_length = 2;

	if(mosq->protocol == mosq_p_mqtt5){
		if(reason_code != 0 || properties){
			remaining_length += 1;
		}

		if(properties){
			remaining_length += mosquitto_property_get_remaining_length(properties);
		}
	}

	rc = packet__alloc(&packet, command, remaining_length);
	if(rc){
		return rc;
	}

	packet__write_uint16(packet, mid);

	if(mosq->protocol == mosq_p_mqtt5){
		if(reason_code != 0 || properties){
			packet__write_byte(packet, reason_code);
		}
		if(properties){
			property__write_all(packet, properties, true);
		}
	}

	return packet__queue(mosq, packet);
}

/* For DISCONNECT, PINGREQ and PINGRESP */
int send__simple_command(struct mosquitto *mosq, uint8_t command)
{
	struct mosquitto__packet *packet = NULL;
	int rc;

	assert(mosq);

	rc = packet__alloc(&packet, command, 0);
	if(rc){
		return rc;
	}

	return packet__queue(mosq, packet);
}
