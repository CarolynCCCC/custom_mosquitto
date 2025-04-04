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
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <utlist.h>

#include "mosquitto_broker_internal.h"
#include "persist.h"
#include "util_mosq.h"

uint32_t db_version;

const unsigned char magic[15] = {0x00, 0xB5, 0x00, 'm','o','s','q','u','i','t','t','o',' ','d','b'};
static long base_msg_count = 0;
static long retained_count = 0;
static long client_count = 0;
static long subscription_count = 0;
static long client_msg_count = 0;

static int persist__restore_sub(const struct mosquitto_subscription *sub);

static struct mosquitto *persist__find_or_add_context(const char *clientid, uint16_t last_mid)
{
	struct mosquitto *context;

	if(!clientid) return NULL;

	context = NULL;
	HASH_FIND(hh_id, db.contexts_by_id, clientid, strlen(clientid), context);
	if(!context){
		context = context__init();
		if(!context) return NULL;
		context->id = mosquitto_strdup(clientid);
		if(!context->id){
			mosquitto_FREE(context);
			return NULL;
		}

		context->clean_start = false;

		context__add_to_by_id(context);
	}
	if(last_mid){
		context->last_mid = last_mid;
	}
	return context;
}


int persist__read_string_len(FILE *db_fptr, char **str, uint16_t len)
{
	char *s = NULL;

	if(len){
		s = mosquitto_malloc(len+1U);
		if(!s){
			log__printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
			return MOSQ_ERR_NOMEM;
		}
		if(fread(s, 1, len, db_fptr) != len){
			mosquitto_FREE(s);
			return MOSQ_ERR_NOMEM;
		}
		s[len] = '\0';
	}

	*str = s;
	return MOSQ_ERR_SUCCESS;
}


int persist__read_string(FILE *db_fptr, char **str)
{
	uint16_t i16temp;
	uint16_t slen;

	if(fread(&i16temp, 1, sizeof(uint16_t), db_fptr) != sizeof(uint16_t)){
		return MOSQ_ERR_INVAL;
	}

	slen = ntohs(i16temp);
	return persist__read_string_len(db_fptr, str, slen);
}


static int persist__client_msg_restore(struct P_client_msg *chunk)
{
	struct mosquitto__client_msg *cmsg;
	struct mosquitto__base_msg *msg;
	struct mosquitto *context;
	struct mosquitto_msg_data *msg_data;

	HASH_FIND(hh, db.msg_store, &chunk->F.store_id, sizeof(chunk->F.store_id), msg);
	if(!msg){
		/* Can't find message - probably expired */
		return MOSQ_ERR_SUCCESS;
	}

	context = persist__find_or_add_context(chunk->clientid, 0);
	if(!context){
		log__printf(NULL, MOSQ_LOG_WARNING, "Warning: Persistence file contains client message with no matching client. File may be corrupt.");
		return 0;
	}

	cmsg = mosquitto_calloc(1, sizeof(struct mosquitto__client_msg));
	if(!cmsg){
		log__printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
		return MOSQ_ERR_NOMEM;
	}

	cmsg->next = NULL;
	cmsg->base_msg = NULL;
	cmsg->data.cmsg_id = ++context->last_cmsg_id;
	cmsg->data.mid = chunk->F.mid;
	cmsg->data.qos = chunk->F.qos;
	cmsg->data.retain = (chunk->F.retain_dup&0xF0)>>4;
	cmsg->data.direction = chunk->F.direction;
	cmsg->data.state = chunk->F.state;
	cmsg->data.dup = chunk->F.retain_dup&0x0F;
	cmsg->data.subscription_identifier = chunk->subscription_identifier;

	cmsg->base_msg = msg;
	db__msg_store_ref_inc(cmsg->base_msg);

	if(cmsg->data.direction == mosq_md_out){
		msg_data = &context->msgs_out;
	}else{
		msg_data = &context->msgs_in;
	}

	if(chunk->F.state == mosq_ms_queued || (chunk->F.qos > 0 && msg_data->inflight_quota == 0)){
		DL_APPEND(msg_data->queued, cmsg);
		db__msg_add_to_queued_stats(msg_data, cmsg);
	}else{
		DL_APPEND(msg_data->inflight, cmsg);
		if(chunk->F.qos > 0 && msg_data->inflight_quota > 0){
			msg_data->inflight_quota--;
		}
		db__msg_add_to_inflight_stats(msg_data, cmsg);
	}

	return MOSQ_ERR_SUCCESS;
}


static int persist__client_chunk_restore(FILE *db_fptr)
{
	int rc = 0;
	struct mosquitto *context;
	struct P_client chunk;

	memset(&chunk, 0, sizeof(struct P_client));

	if(db_version == 6 || db_version == 5){
		rc = persist__chunk_client_read_v56(db_fptr, &chunk, db_version);
	}else{
		rc = persist__chunk_client_read_v234(db_fptr, &chunk, db_version);
	}
	if(rc > 0){
		return rc;
	}else if(rc < 0){
		/* Client not loaded, but otherwise not an error */
		log__printf(NULL, MOSQ_LOG_WARNING, "Warning: Empty client entry found in persistence database, it may be corrupt.");
		return MOSQ_ERR_SUCCESS;
	}

	context = persist__find_or_add_context(chunk.clientid, chunk.F.last_mid);
	if(context){
		context->session_expiry_time = chunk.F.session_expiry_time;
		context->session_expiry_interval = chunk.F.session_expiry_interval;
		if(chunk.username && !context->username){
			/* username is not freed here, it is now owned by context */
			context->username = chunk.username;
			chunk.username = NULL;
		}
		/* in per_listener_settings mode, try to find the listener by persisted port */
		if(db.config->per_listener_settings && !context->listener && chunk.F.listener_port > 0){
			for(int i=0; i < db.config->listener_count; i++){
				if(db.config->listeners[i].port == chunk.F.listener_port){
					context->listener = &db.config->listeners[i];
					break;
				}
			}
		}
		session_expiry__add_from_persistence(context, chunk.F.session_expiry_time);
	}else{
		rc = 1;
	}

	mosquitto_FREE(chunk.clientid);
	mosquitto_FREE(chunk.username);
	if(rc == 0) client_count++;
	return rc;
}


static int persist__client_msg_chunk_restore(FILE *db_fptr, uint32_t length)
{
	struct P_client_msg chunk;
	int rc;

	memset(&chunk, 0, sizeof(struct P_client_msg));

	if(db_version == 6 || db_version == 5){
		rc = persist__chunk_client_msg_read_v56(db_fptr, &chunk, length);
	}else{
		rc = persist__chunk_client_msg_read_v234(db_fptr, &chunk);
	}
	if(rc){
		return rc;
	}

	rc = persist__client_msg_restore(&chunk);
	mosquitto_FREE(chunk.clientid);

	if(rc == 0) client_msg_count++;
	return rc;
}


static int persist__base_msg_chunk_restore(FILE *db_fptr, uint32_t length)
{
	struct P_base_msg chunk;
	struct mosquitto__base_msg *base_msg = NULL;
	int64_t message_expiry_interval64;
	uint32_t message_expiry_interval;
	uint32_t *p_message_expiry_interval = NULL;
	int rc = 0;

	memset(&chunk, 0, sizeof(struct P_base_msg));

	if(db_version == 6 || db_version == 5){
		rc = persist__chunk_base_msg_read_v56(db_fptr, &chunk, length);
	}else{
		rc = persist__chunk_base_msg_read_v234(db_fptr, &chunk, db_version);
	}
	if(rc) return rc;

	if(chunk.F.source_port){
		for(int i=0; i<db.config->listener_count; i++){
			if(db.config->listeners[i].port == chunk.F.source_port){
				chunk.source.listener = &db.config->listeners[i];
				break;
			}
		}
	}

	if(chunk.F.expiry_time > 0){
		message_expiry_interval64 = chunk.F.expiry_time - time(NULL);
		if(message_expiry_interval64 < 0 || message_expiry_interval64 > UINT32_MAX){
			/* Expired message */
			rc = MOSQ_ERR_SUCCESS;
			goto cleanup;
		}else{
			message_expiry_interval = (uint32_t)message_expiry_interval64;
		}
		p_message_expiry_interval = &message_expiry_interval;
	}

	base_msg = mosquitto_calloc(1, sizeof(struct mosquitto__base_msg));
	if(base_msg == NULL){
		log__printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
		rc = MOSQ_ERR_NOMEM;
		goto cleanup;
	}

	base_msg->data.store_id = chunk.F.store_id;
	base_msg->data.source_mid = chunk.F.source_mid;
	base_msg->data.topic = chunk.topic;
	base_msg->data.qos = chunk.F.qos;
	base_msg->data.payloadlen = chunk.F.payloadlen;
	base_msg->data.retain = chunk.F.retain;
	base_msg->data.properties = chunk.properties;
	base_msg->data.payload = chunk.payload;
	base_msg->source_listener = chunk.source.listener;

	rc = db__message_store(&chunk.source, base_msg, p_message_expiry_interval,
			mosq_mo_client);

	mosquitto_FREE(chunk.source.id);
	mosquitto_FREE(chunk.source.username);

	if(rc == MOSQ_ERR_SUCCESS){
		base_msg_count++;
		return MOSQ_ERR_SUCCESS;
	}else{
		return rc;
	}
cleanup:
	mosquitto_FREE(chunk.source.id);
	mosquitto_FREE(chunk.source.username);
	mosquitto_FREE(chunk.topic);
	mosquitto_FREE(chunk.payload);
	return rc;
}

static int persist__retain_chunk_restore(FILE *db_fptr)
{
	struct mosquitto__base_msg *base_msg;
	struct P_retain chunk;
	int rc;
	char **split_topics;
	char *local_topic;

	memset(&chunk, 0, sizeof(struct P_retain));

	if(db_version == 6 || db_version == 5){
		rc = persist__chunk_retain_read_v56(db_fptr, &chunk);
	}else{
		rc = persist__chunk_retain_read_v234(db_fptr, &chunk);
	}
	if(rc){
		return rc;
	}

	HASH_FIND(hh, db.msg_store, &chunk.F.store_id, sizeof(chunk.F.store_id), base_msg);
	if(base_msg){
		if(sub__topic_tokenise(base_msg->data.topic, &local_topic, &split_topics, NULL)) return 1;
		retain__store(base_msg->data.topic, base_msg, split_topics, true);
		mosquitto_FREE(local_topic);
		mosquitto_FREE(split_topics);
		retained_count++;
	}else{
		/* Can't find the message - probably expired */
	}
	return MOSQ_ERR_SUCCESS;
}

static int persist__sub_chunk_restore(FILE *db_fptr)
{
	struct P_sub chunk;
	int rc;
	struct mosquitto_subscription sub;

	memset(&chunk, 0, sizeof(struct P_sub));

	if(db_version == 6 || db_version == 5){
		rc = persist__chunk_sub_read_v56(db_fptr, &chunk);
	}else{
		rc = persist__chunk_sub_read_v234(db_fptr, &chunk);
	}
	if(rc){
		return rc;
	}

	sub.clientid = chunk.clientid;
	sub.topic_filter = chunk.topic;
	sub.options = chunk.F.qos | chunk.F.options;
	sub.identifier = chunk.F.identifier;
	rc = persist__restore_sub(&sub);

	mosquitto_FREE(chunk.clientid);
	mosquitto_FREE(chunk.topic);
	if(rc == 0) subscription_count++;

	return rc;
}


int persist__chunk_header_read(FILE *db_fptr, uint32_t *chunk, uint32_t *length)
{
	if(db_version == 6 || db_version == 5){
		return persist__chunk_header_read_v56(db_fptr, chunk, length);
	}else{
		return persist__chunk_header_read_v234(db_fptr, chunk, length);
	}
}


int persist__restore(void)
{
	FILE *fptr;
	char header[15];
	int rc = 0;
	uint32_t crc;
	uint32_t i32temp;
	uint32_t chunk, length;
	size_t rlen;
	char *err;
	struct PF_cfg cfg_chunk;

	assert(db.config);

	if(!db.config->persistence || db.config->persistence_filepath == NULL){
		return MOSQ_ERR_SUCCESS;
	}

	db.msg_store = NULL;
	base_msg_count = 0;
	retained_count = 0;
	client_count = 0;
	subscription_count = 0;
	client_msg_count = 0;

	fptr = mosquitto_fopen(db.config->persistence_filepath, "rb", true);
	if(fptr == NULL) return MOSQ_ERR_SUCCESS;
	rlen = fread(&header, 1, 15, fptr);
	if(rlen == 0){
		fclose(fptr);
		log__printf(NULL, MOSQ_LOG_WARNING, "Warning: Persistence file is empty.");
		return 0;
	}else if(rlen != 15){
		goto error;
	}
	if(!memcmp(header, magic, 15)){
		/* Restore DB as normal */
		read_e(fptr, &crc, sizeof(uint32_t));
		read_e(fptr, &i32temp, sizeof(uint32_t));
		db_version = ntohl(i32temp);
		/* IMPORTANT - this is where compatibility checks are made.
		 * Is your DB change still compatible with previous versions?
		 */
		if(db_version != MOSQ_DB_VERSION){
			if(db_version == 5){
				/* Addition of username and listener_port to client chunk in v6 */
			}else if(db_version == 4){
			}else if(db_version == 3){
				/* Addition of source_username and source_port to msg_store chunk in v4, v1.5.6 */
			}else if(db_version == 2){
				/* Addition of disconnect_t to client chunk in v3. */
			}else{
				fclose(fptr);
				log__printf(NULL, MOSQ_LOG_ERR, "Error: Unsupported persistent database format version %d (need version %d).", db_version, MOSQ_DB_VERSION);
				return 1;
			}
		}

		while(persist__chunk_header_read(fptr, &chunk, &length) == MOSQ_ERR_SUCCESS){
			switch(chunk){
				case DB_CHUNK_CFG:
					if(db_version == 6 || db_version == 5){
						if(persist__chunk_cfg_read_v56(fptr, &cfg_chunk)){
							fclose(fptr);
							return 1;
						}
					}else{
						if(persist__chunk_cfg_read_v234(fptr, &cfg_chunk)){
							fclose(fptr);
							return 1;
						}
					}
					if(cfg_chunk.dbid_size != sizeof(dbid_t)){
						log__printf(NULL, MOSQ_LOG_ERR, "Error: Incompatible database configuration (dbid size is %d bytes, expected %lu)",
								cfg_chunk.dbid_size, (unsigned long)sizeof(dbid_t));
						fclose(fptr);
						return 1;
					}
					db.last_db_id = cfg_chunk.last_db_id;
					break;

				case DB_CHUNK_BASE_MSG:
					if(persist__base_msg_chunk_restore(fptr, length)){
						fclose(fptr);
						return 1;
					}
					break;

				case DB_CHUNK_CLIENT_MSG:
					if(persist__client_msg_chunk_restore(fptr, length)){
						fclose(fptr);
						return 1;
					}
					break;

				case DB_CHUNK_RETAIN:
					if(persist__retain_chunk_restore(fptr)){
						fclose(fptr);
						return 1;
					}
					break;

				case DB_CHUNK_SUB:
					if(persist__sub_chunk_restore(fptr)){
						fclose(fptr);
						return 1;
					}
					break;

				case DB_CHUNK_CLIENT:
					if(persist__client_chunk_restore(fptr)){
						fclose(fptr);
						return 1;
					}
					break;

				default:
					log__printf(NULL, MOSQ_LOG_WARNING, "Warning: Unsupported chunk \"%d\" in persistent database file. Ignoring.", chunk);
					fseek(fptr, length, SEEK_CUR);
					break;
			}
		}
	}else{
		log__printf(NULL, MOSQ_LOG_ERR, "Error: Unable to restore persistent database. Unrecognised file format.");
		rc = 1;
	}

	fclose(fptr);

	log__printf(NULL, MOSQ_LOG_INFO, "Restored %ld base messages", base_msg_count);
	log__printf(NULL, MOSQ_LOG_INFO, "Restored %ld retained messages", retained_count);
	log__printf(NULL, MOSQ_LOG_INFO, "Restored %ld clients", client_count);
	log__printf(NULL, MOSQ_LOG_INFO, "Restored %ld subscriptions", subscription_count);
	log__printf(NULL, MOSQ_LOG_INFO, "Restored %ld client messages", client_msg_count);

	return rc;
error:
	err = strerror(errno);
	log__printf(NULL, MOSQ_LOG_ERR, "Error: %s.", err);
	if(fptr) fclose(fptr);
	return 1;
}

static int persist__restore_sub(const struct mosquitto_subscription *sub)
{
	struct mosquitto *context;

	if(!sub->clientid){
		log__printf(NULL, MOSQ_LOG_WARNING, "Warning: Persistence found a subscription with no client id, ignoring.");
		return MOSQ_ERR_SUCCESS;
	}
	if(!sub->topic_filter){
		log__printf(NULL, MOSQ_LOG_WARNING, "Warning: Persistence found a subscription with no topic filter, ignoring.");
		return MOSQ_ERR_SUCCESS;
	}

	context = persist__find_or_add_context(sub->clientid, 0);
	if(!context) return 1;
	return sub__add(context, sub);
}

#endif
