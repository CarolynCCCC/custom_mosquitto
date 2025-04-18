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
#include "send_mosq.h"
#include "util_mosq.h"
#include "will_mosq.h"
#include "sys_tree.h"


int handle__disconnect(struct mosquitto *context)
{
	int rc;
	uint8_t reason_code = 0;
	mosquitto_property *properties = NULL;

	if(!context){
		return MOSQ_ERR_INVAL;
	}

	if(context->in_packet.command != CMD_DISCONNECT){
		return MOSQ_ERR_MALFORMED_PACKET;
	}

	if(context->protocol == mosq_p_mqtt5 && context->in_packet.remaining_length > 0){
		/* FIXME - must handle reason code */
		rc = packet__read_byte(&context->in_packet, &reason_code);
		if(rc) return rc;

		if(context->in_packet.remaining_length > 1){
			rc = property__read_all(CMD_DISCONNECT, &context->in_packet, &properties);
			if(rc) return rc;
		}
	}
	rc = property__process_disconnect(context, &properties);
	if(rc){
		mosquitto_property_free_all(&properties);
		return rc;
	}
	mosquitto_property_free_all(&properties); /* FIXME - TEMPORARY UNTIL PROPERTIES PROCESSED */

	if(context->in_packet.pos != context->in_packet.remaining_length){
		return MOSQ_ERR_PROTOCOL;
	}
	log__printf(NULL, MOSQ_LOG_DEBUG, "Received DISCONNECT from %s", context->id);
	if(context->protocol == mosq_p_mqtt311 || context->protocol == mosq_p_mqtt5){
		if((context->in_packet.command&0x0F) != 0x00){
			do_disconnect(context, MOSQ_ERR_PROTOCOL);
			return MOSQ_ERR_PROTOCOL;
		}
	}
	if(context->username){
        char user_state_topic[256];
        snprintf(user_state_topic, sizeof(user_state_topic), 
                "$SYS/broker/user_state/%s", context->username);
        char payload = '0';
        db__messages_easy_queue(context, user_state_topic, 1, 1, 
                              &payload, 1, MSG_EXPIRY_INFINITE, NULL);

        /* Clean up user metrics if enabled */
        if(db.config->user_stats){
            user_metrics__cleanup(context);
        }
    }

	if(reason_code == MQTT_RC_DISCONNECT_WITH_WILL_MSG){
		mosquitto__set_state(context, mosq_cs_disconnect_with_will);
	}else{
		will__clear(context);
		mosquitto__set_state(context, mosq_cs_disconnecting);
	}
	do_disconnect(context, MOSQ_ERR_SUCCESS);
	return MOSQ_ERR_SUCCESS;
}
