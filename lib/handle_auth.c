/*
Copyright (c) 2018-2021 Roger Light <roger@atchoo.org>

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

#include "callbacks.h"
#include "logging_mosq.h"
#include "mosquitto_internal.h"
#include "mosquitto/mqtt_protocol.h"
#include "packet_mosq.h"
#include "property_mosq.h"
#include "read_handle.h"


int handle__auth(struct mosquitto *mosq)
{
	int rc = 0;
	uint8_t reason_code;
	char *auth_method = NULL;
	void *auth_data = NULL;
	uint16_t auth_data_len = 0;
	mosquitto_property *properties = NULL;

	if(!mosq) return MOSQ_ERR_INVAL;
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s received AUTH", SAFE_PRINT(mosq->id));

	if(mosq->protocol != mosq_p_mqtt5){
		return MOSQ_ERR_PROTOCOL;
	}
	if(mosq->in_packet.command != CMD_AUTH){
		return MOSQ_ERR_MALFORMED_PACKET;
	}

	if(packet__read_byte(&mosq->in_packet, &reason_code)) return 1;

	rc = property__read_all(CMD_AUTH, &mosq->in_packet, &properties);
	if(rc) return rc;

	mosquitto_property_read_string(properties, MQTT_PROP_AUTHENTICATION_METHOD, &auth_method, false);
	mosquitto_property_read_binary(properties, MQTT_PROP_AUTHENTICATION_DATA, &auth_data, &auth_data_len, false);
	rc = callback__on_ext_auth(mosq, auth_method, auth_data_len, auth_data, properties);
	mosquitto_property_free_all(&properties);

	return rc;
}
