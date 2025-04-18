/*
Copyright (c) 2020-2024 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License 2.0
and Eclipse Distribution License v1.0 which accompany this distribution.

The Eclipse Public License is available at
   https://www.eclipse.org/legal/epl-2.0/
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.

SPDX-License-Identifier: EPL-2.0 OR BSD-3-Clause

Contributors:
   Abilio Marques - initial implementation and documentation.
   Ben Hardill - convert to regex based multi-tenant
*/

/*
 * A very simple Multi-Tenant broker plugin
 *
 * A enhanced version of the mosquitto_topic_jail example.
 *
 * It uses a supplied regex to extract a "team" name from the username.
 * The regex must include a single capture group that extracts the team name,
 * with a default '[a-z0-9]+@([a-z0-9]+)'
 * 
 * e.g. username 'foo@bar' would give a team of 'bar'
 * 
 * Compile with:
 *   gcc -I<path to mosquitto-repo/include> -fPIC -shared mosquitto_multi_tenant.c -o mosquitto_multi_tenant.so
 *
 * Use in config with:
 *
 *   plugin /path/to/mosquitto_multi_tenant.so
 *
 * Note that this only works on Mosquitto 2.1 or later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosquitto.h"
#include "mosquitto_internal.h"
#include "mosquitto_broker.h"
#include "mosquitto_plugin.h"

#define PLUGIN_NAME "multi-tenant"
#define PLUGIN_VERSION "1.1.0"

#define UNUSED(A) (void)(A)

/* Windows compatibility */
#ifdef _WIN32
#define PLUGIN_EXPORT __declspec(dllexport)
#define strncasecmp _strnicmp
#else
#define PLUGIN_EXPORT
#endif

MOSQUITTO_PLUGIN_DECLARE_VERSION(5);

static mosquitto_plugin_id_t *mosq_pid = NULL;

static char* get_team(const char *str) {
	const char *at_sign = strchr(str, '@');
	if (!at_sign) return NULL;
	
	// Skip the @ character
	at_sign++;
	
	// Calculate length of team name
	size_t team_len = strlen(at_sign);
	if (team_len == 0) return NULL;
	
	// Allocate and copy team name
	char *team = mosquitto_malloc(team_len + 1);
	if (!team) return NULL;
	
	strcpy(team, at_sign);
	return team;
}

static int parse_shared_subscription(const char *topic, char **share_group, char **topic_part) {
	if (strncmp(topic, "$share/", 7) != 0) return MOSQ_ERR_NOMEM;
	
	const char *group_start = topic + 7;
	const char *group_end = strchr(group_start, '/');
	if (!group_end) return MOSQ_ERR_NOMEM;
	
	size_t group_len = group_end - group_start;
	*share_group = mosquitto_malloc(group_len + 1);
	if (!*share_group) return MOSQ_ERR_NOMEM;
	
	strncpy(*share_group, group_start, group_len);
	(*share_group)[group_len] = '\0';
	
	const char *topic_start = group_end + 1;
	size_t topic_len = strlen(topic_start);
	*topic_part = mosquitto_malloc(topic_len + 1);
	if (!*topic_part) {
		mosquitto_free(*share_group);
		return MOSQ_ERR_NOMEM;
	}
	
	strcpy(*topic_part, topic_start);
	return MOSQ_ERR_SUCCESS;
}

static int connect_callback(int event, void *event_data, void *userdata)
{
	struct mosquitto_evt_message *ed = event_data;
	const char *id, *username;
	char *new_id;
	size_t idlen, new_id_len;

	UNUSED(event);
	UNUSED(userdata);
	username = mosquitto_client_username(ed->client);

	if (!username) {
		return MOSQ_ERR_SUCCESS;
	}

	id = mosquitto_client_id(ed->client);
	idlen = strlen(id);

	const char *team = get_team(username);
	if(!team){
		/* will only modify the client id of team clients */
		return MOSQ_ERR_SUCCESS;
	}

	/* calculate new client id length */
	new_id_len = strlen(team) + sizeof('@') + idlen + 1;

	new_id = mosquitto_calloc(1, new_id_len);
	if(new_id == NULL){
		return MOSQ_ERR_NOMEM;
	}

	/* generate new client id with team name */
	snprintf(new_id, new_id_len, "%s@%s", id, team);

	mosquitto_free((void *)team);

	mosquitto_set_clientid(ed->client, new_id);

	return MOSQ_ERR_SUCCESS;
}


static int callback_message_in(int event, void *event_data, void *userdata)
{
	struct mosquitto_evt_message *ed = event_data;
	char *new_topic;
	size_t new_topic_len;

	UNUSED(event);
	UNUSED(userdata);

	const char *username = mosquitto_client_username(ed->client);
	if (!username) {
		return MOSQ_ERR_SUCCESS;
	}

	const char *team = get_team(username);
	if(!team){
		/* will only modify the topic of team clients */
		return MOSQ_ERR_SUCCESS;
	}

	/* put the team on front of the topic */

	/* calculate the length of the new payload */
	new_topic_len = strlen(team) + sizeof('/') + strlen(ed->topic) + 1;

	/* Allocate some memory - use
	 * mosquitto_calloc/mosquitto_malloc/mosquitto_strdup when allocating, to
	 * allow the broker to track memory usage */
	new_topic = mosquitto_calloc(1, new_topic_len);
	if(new_topic == NULL){
		return MOSQ_ERR_NOMEM;
	}

	/* prepend the team to the topic */
	snprintf(new_topic, new_topic_len, "%s/%s", team, ed->topic);

	mosquitto_free((void *)team);

	/* Assign the new topic to the event data structure. You
	 * must *not* free the original topic, it will be handled by the
	 * broker. */
	ed->topic = new_topic;

	return MOSQ_ERR_SUCCESS;
}

static int callback_message_out(int event, void *event_data, void *userdata)
{
	struct mosquitto_evt_message *ed = event_data;
	size_t team_len;

	UNUSED(event);
	UNUSED(userdata);

	const char *username = mosquitto_client_username(ed->client);
	if (!username) {
		return MOSQ_ERR_SUCCESS;
	}

	const char *team = get_team(username);
	if(!team){
		/* will only modify the topic of team clients */
		return MOSQ_ERR_SUCCESS;
	}

	/* remove the team from the front of the topic */
	team_len = strlen(team);

	if(strlen(ed->topic) <= team_len + 1){
		/* the topic is not long enough to contain the
		 * team + '/' */
		return MOSQ_ERR_SUCCESS;
	}

	if(!strncmp(team, ed->topic, team_len) && ed->topic[team_len] == '/'){
		/* Allocate some memory - use
		 * mosquitto_calloc/mosquitto_malloc/mosquitto_strdup when allocating, to
		 * allow the broker to track memory usage */

		/* skip the team + '/' */
		char *new_topic = mosquitto_strdup(ed->topic + team_len + 1);

		if(new_topic == NULL){
			return MOSQ_ERR_NOMEM;
		}

		/* Assign the new topic to the event data structure. You
		 * must *not* free the original topic, it will be handled by the
		 * broker. */
		ed->topic = new_topic;
	}

	mosquitto_free((void *)team);

	return MOSQ_ERR_SUCCESS;
}

static int callback_subscribe(int event, void *event_data, void *userdata)
{
	struct mosquitto_evt_subscribe *ed = event_data;
	char *new_sub, *share_group, *topic;
	size_t new_sub_len;

	UNUSED(event);
	UNUSED(userdata);

	const char *username = mosquitto_client_username(ed->client);
	if (!username) {
		return MOSQ_ERR_SUCCESS;
	}

	const char *team = get_team(username);
	if(!team){
		/* will only modify the topic of team clients */
		return MOSQ_ERR_SUCCESS;
	}

	if (!strncmp(ed->data.topic_filter, "$share/", 7)) {
		new_sub_len = strlen(team) + (sizeof('/') * 2) + strlen(ed->data.topic_filter) + 1;

		new_sub = mosquitto_calloc(1, new_sub_len);
		if(new_sub == NULL){
			return MOSQ_ERR_NOMEM;
		}

		int rc = parse_shared_subscription(ed->data.topic_filter, &share_group, &topic);
		if (rc == MOSQ_ERR_SUCCESS) {
			snprintf(new_sub, new_sub_len, "%s/%s/%s", share_group, team, topic);
			
			mosquitto_free((void *)share_group);
			mosquitto_free((void *)topic);
		} else {
			return rc;
		}
		
	} else {
		/* put the team on front of the topic */

		/* calculate the length of the new payload */
		new_sub_len = strlen(team) + sizeof('/') + strlen(ed->data.topic_filter) + 1;

		/* Allocate some memory - use
		* mosquitto_calloc/mosquitto_malloc/mosquitto_strdup when allocating, to
		* allow the broker to track memory usage */
		new_sub = mosquitto_calloc(1, new_sub_len);
		if(new_sub == NULL){
			return MOSQ_ERR_NOMEM;
		}

		/* prepend the team to the subscription */
		snprintf(new_sub, new_sub_len, "%s/%s", team, ed->data.topic_filter);
	}
	/* Assign the new topic to the event data structure. You
	 * must *not* free the original topic, it will be handled by the
	 * broker. */
	ed->data.topic_filter = new_sub;

	mosquitto_free((void *) team);

	return MOSQ_ERR_SUCCESS;
}

static int callback_unsubscribe(int event, void *event_data, void *userdata)
{
	struct mosquitto_evt_unsubscribe *ed = event_data;
	char *new_sub, *share_group, *topic;
	size_t new_sub_len;

	UNUSED(event);
	UNUSED(userdata);

	const char *username = mosquitto_client_username(ed->client);
	if (!username) {
		return MOSQ_ERR_SUCCESS;
	}

	const char *team = get_team(username);
	if(!team){
		/* will only modify the topic of team clients */
		// mosquitto_free((void *)team);
		return MOSQ_ERR_SUCCESS;
	}

		if (!strncmp(ed->data.topic_filter, "$share/", 7)) {
		new_sub_len = strlen(team) + (sizeof('/') * 2) + strlen(ed->data.topic_filter) + 1;

		new_sub = mosquitto_calloc(1, new_sub_len);
		if(new_sub == NULL){
			return MOSQ_ERR_NOMEM;
		}

		int rc = parse_shared_subscription(ed->data.topic_filter, &share_group, &topic);
		if (rc == MOSQ_ERR_SUCCESS) {
			snprintf(new_sub, new_sub_len, "%s/%s/%s", share_group, team, topic);
			
			mosquitto_free((void *)share_group);
			mosquitto_free((void *)topic);
		} else {
			return rc;
		}
		
	} else {

		/* put the team on front of the topic */

		/* calculate the length of the new payload */
		new_sub_len = strlen(team) + sizeof('/') + strlen(ed->data.topic_filter) + 1;

		/* Allocate some memory - use
		* mosquitto_calloc/mosquitto_malloc/mosquitto_strdup when allocating, to
		* allow the broker to track memory usage */
		new_sub = mosquitto_calloc(1, new_sub_len);
		if(new_sub == NULL){
			return MOSQ_ERR_NOMEM;
		}

		/* prepend the team to the subscription */
		snprintf(new_sub, new_sub_len, "%s/%s", team, ed->data.topic_filter);
	}
	/* Assign the new topic to the event data structure. You
	 * must *not* free the original topic, it will be handled by the
	 * broker. */
	ed->data.topic_filter = new_sub;

	mosquitto_free((void *)team);

	return MOSQ_ERR_SUCCESS;
}


PLUGIN_EXPORT int mosquitto_plugin_init(mosquitto_plugin_id_t *identifier, void **user_data, struct mosquitto_opt *opts, int opt_count)
{
	UNUSED(opts);
	UNUSED(opt_count);
	UNUSED(user_data);

	mosq_pid = identifier;
	mosquitto_plugin_set_info(identifier, PLUGIN_NAME, PLUGIN_VERSION);

	int rc;
	rc = mosquitto_callback_register(mosq_pid, MOSQ_EVT_CONNECT, connect_callback, NULL, NULL);
	if(rc) return rc;
	rc = mosquitto_callback_register(mosq_pid, MOSQ_EVT_MESSAGE_IN, callback_message_in, NULL, NULL);
	if(rc) return rc;
	rc = mosquitto_callback_register(mosq_pid, MOSQ_EVT_MESSAGE_OUT, callback_message_out, NULL, NULL);
	if(rc) return rc;
	rc = mosquitto_callback_register(mosq_pid, MOSQ_EVT_SUBSCRIBE, callback_subscribe, NULL, NULL);
	if(rc) return rc;
	rc = mosquitto_callback_register(mosq_pid, MOSQ_EVT_UNSUBSCRIBE, callback_unsubscribe, NULL, NULL);
	return rc;
}

/* Add cleanup function with PLUGIN_EXPORT */
PLUGIN_EXPORT int mosquitto_plugin_cleanup(void *user_data, struct mosquitto_opt *opts, int opt_count)
{
	UNUSED(user_data);
	UNUSED(opts);
	UNUSED(opt_count);

	return MOSQ_ERR_SUCCESS;
}
