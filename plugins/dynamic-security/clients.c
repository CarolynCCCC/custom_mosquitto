/*
Copyright (c) 2020-2021 Roger Light <roger@atchoo.org>

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

#include <cjson/cJSON.h>
#include <stdio.h>
#include <uthash.h>

#include "dynamic_security.h"
#include "json_help.h"

struct connection_array_context{
	const char *username;
	cJSON *j_connections;
};

/* ################################################################
 * #
 * # Function declarations
 * #
 * ################################################################ */

static int dynsec__remove_client_from_all_groups(struct dynsec__data *data, const char *username);
static void client__remove_all_roles(struct dynsec__client *client);

/* ################################################################
 * #
 * # Local variables
 * #
 * ################################################################ */

/* ################################################################
 * #
 * # Utility functions
 * #
 * ################################################################ */

static int client_cmp(void *a, void *b)
{
	struct dynsec__client *client_a = a;
	struct dynsec__client *client_b = b;

	return strcmp(client_a->username, client_b->username);
}

struct dynsec__client *dynsec_clients__find(struct dynsec__data *data, const char *username)
{
	struct dynsec__client *client = NULL;

	if(username){
		HASH_FIND(hh, data->clients, username, strlen(username), client);
	}
	return client;
}


static void client__free_item(struct dynsec__data *data, struct dynsec__client *client)
{
	struct dynsec__client *client_found;
	if(client == NULL) return;

	client_found = dynsec_clients__find(data, client->username);
	if(client_found){
		HASH_DEL(data->clients, client_found);
	}
	dynsec_rolelist__cleanup(&client->rolelist);
	dynsec__remove_client_from_all_groups(data, client->username);
	mosquitto_pw_cleanup(client->pw);
	mosquitto_free(client->text_name);
	mosquitto_free(client->text_description);
	mosquitto_free(client->clientid);
	mosquitto_free(client);
}

void dynsec_clients__cleanup(struct dynsec__data *data)
{
	struct dynsec__client *client, *client_tmp;

	HASH_ITER(hh, data->clients, client, client_tmp){
		client__free_item(data, client);
	}
}

/* ################################################################
 * #
 * # Config file load and save
 * #
 * ################################################################ */

int dynsec_clients__config_load(struct dynsec__data *data, cJSON *tree)
{
	cJSON *j_clients, *j_client = NULL, *j_roles, *j_role;
	struct dynsec__client *client;
	struct dynsec__role *role;
	int priority;

	j_clients = cJSON_GetObjectItem(tree, "clients");
	if(j_clients == NULL){
		return 0;
	}

	if(cJSON_IsArray(j_clients) == false){
		return 1;
	}
	cJSON_ArrayForEach(j_client, j_clients){
		if(cJSON_IsObject(j_client) == true){
			/* Username */
			const char *username;
			if(json_get_string(j_client, "username", &username, false) != MOSQ_ERR_SUCCESS){
				continue;
			}
			size_t username_len = strlen(username);
			if(username_len == 0){
				continue;
			}
			if(dynsec_clients__find(data, username)){
				continue;
			}

			client = mosquitto_calloc(1, sizeof(struct dynsec__client) + username_len + 1);
			if(client == NULL){
				return MOSQ_ERR_NOMEM;
			}
			strncpy(client->username, username, username_len);

			bool disabled;
			if(json_get_bool(j_client, "disabled", &disabled, false, false) == MOSQ_ERR_SUCCESS){
				client->disabled = disabled;
			}

			const char *password;
			if(json_get_string(j_client, "encoded_password", &password, false) == MOSQ_ERR_SUCCESS){
				if(!client->pw && mosquitto_pw_new(&client->pw, MOSQ_PW_DEFAULT)){
					return MOSQ_ERR_NOMEM;
				}
				if(mosquitto_pw_decode(client->pw, password) == MOSQ_ERR_NOMEM){
					return MOSQ_ERR_NOMEM;
				}
			}else{
				/* sha512-pbkdf2 only */
				int iterations;
				const char *salt;
				json_get_int(j_client, "iterations", &iterations, true, 0);
				if(json_get_string(j_client, "salt", &salt, false) == MOSQ_ERR_SUCCESS
						&& json_get_string(j_client, "password", &password, false) == MOSQ_ERR_SUCCESS
						&& iterations > 0){

					char buf[1024];
					if(!client->pw && mosquitto_pw_new(&client->pw, MOSQ_PW_SHA512_PBKDF2)){
						return MOSQ_ERR_NOMEM;
					}
					snprintf(buf, sizeof(buf), "$7$%d$%s$%s", iterations, salt, password);
					mosquitto_pw_decode(client->pw, buf);
				}else{
					mosquitto_pw_set_valid(client->pw, false);
				}
			}

			/* Client id */
			const char *clientid;
			if(json_get_string(j_client, "clientid", &clientid, false) == MOSQ_ERR_SUCCESS){
				client->clientid = mosquitto_strdup(clientid);
				if(client->clientid == NULL){
					mosquitto_free(client);
					continue;
				}
			}

			/* Text name */
			const char *textname;
			if(json_get_string(j_client, "textname", &textname, false) == MOSQ_ERR_SUCCESS){
				client->text_name = mosquitto_strdup(textname);
				if(client->text_name == NULL){
					mosquitto_free(client->clientid);
					mosquitto_free(client);
					continue;
				}
			}

			/* Text description */
			const char *textdescription;
			if(json_get_string(j_client, "textdescription", &textdescription, false) == MOSQ_ERR_SUCCESS){
				client->text_description = mosquitto_strdup(textdescription);
				if(client->text_description == NULL){
					mosquitto_free(client->text_name);
					mosquitto_free(client->clientid);
					mosquitto_free(client);
					continue;
				}
			}

			/* Roles */
			j_roles = cJSON_GetObjectItem(j_client, "roles");
			if(j_roles && cJSON_IsArray(j_roles)){
				cJSON_ArrayForEach(j_role, j_roles){
					if(cJSON_IsObject(j_role)){
						const char *rolename;
						if(json_get_string(j_role, "rolename", &rolename, false) == MOSQ_ERR_SUCCESS){
							json_get_int(j_role, "priority", &priority, true, -1);
							if(priority > PRIORITY_MAX) priority = PRIORITY_MAX;
							if(priority < -PRIORITY_MAX) priority = -PRIORITY_MAX;
							role = dynsec_roles__find(data, rolename);
							dynsec_rolelist__client_add(client, role, priority);
						}
					}
				}
			}

			HASH_ADD(hh, data->clients, username, username_len, client);
		}
	}
	HASH_SORT(data->clients, client_cmp);

	return 0;
}


static int dynsec__config_add_clients(struct dynsec__data *data, cJSON *j_clients)
{
	struct dynsec__client *client, *client_tmp;
	cJSON *j_client, *j_roles;

	HASH_ITER(hh, data->clients, client, client_tmp){
		j_client = cJSON_CreateObject();
		if(j_client == NULL) return 1;
		cJSON_AddItemToArray(j_clients, j_client);

		if(cJSON_AddStringToObject(j_client, "username", client->username) == NULL
				|| (client->clientid && cJSON_AddStringToObject(j_client, "clientid", client->clientid) == NULL)
				|| (client->text_name && cJSON_AddStringToObject(j_client, "textname", client->text_name) == NULL)
				|| (client->text_description && cJSON_AddStringToObject(j_client, "textdescription", client->text_description) == NULL)
				|| (client->disabled && cJSON_AddBoolToObject(j_client, "disabled", true) == NULL)
				){

			return 1;
		}

		j_roles = dynsec_rolelist__all_to_json(client->rolelist);
		if(j_roles == NULL){
			return 1;
		}
		cJSON_AddItemToObject(j_client, "roles", j_roles);

		if(mosquitto_pw_is_valid(client->pw)){
			if(cJSON_AddStringToObject(j_client, "encoded_password", mosquitto_pw_get_encoded(client->pw)) == NULL){
				return 1;
			}
		}
	}

	return 0;
}


int dynsec_clients__config_save(struct dynsec__data *data, cJSON *tree)
{
	cJSON *j_clients;

	if((j_clients = cJSON_AddArrayToObject(tree, "clients")) == NULL){
		return 1;
	}
	if(dynsec__config_add_clients(data, j_clients)){
		return 1;
	}

	return 0;
}


int dynsec_clients__process_create(struct dynsec__data *data, struct mosquitto_control_cmd *cmd)
{
	const char *username, *password, *clientid = NULL;
	const char *text_name, *text_description;
	struct dynsec__client *client;
	int rc;
	cJSON *j_groups, *j_group;
	int priority;
	const char *admin_clientid, *admin_username;
	size_t username_len;

	if(json_get_string(cmd->j_command, "username", &username, false) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing username");
		return MOSQ_ERR_INVAL;
	}
	username_len = strlen(username);
	if(username_len == 0){
		mosquitto_control_command_reply(cmd, "Empty username");
		return MOSQ_ERR_INVAL;
	}
	if(mosquitto_validate_utf8(username, (int)username_len) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Username not valid UTF-8");
		return MOSQ_ERR_INVAL;
	}

	if(json_get_string(cmd->j_command, "password", &password, true) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing password");
		return MOSQ_ERR_INVAL;
	}

	if(json_get_string(cmd->j_command, "clientid", &clientid, true) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing client id");
		return MOSQ_ERR_INVAL;
	}
	if(clientid && mosquitto_validate_utf8(clientid, (int)strlen(clientid)) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Client ID not valid UTF-8");
		return MOSQ_ERR_INVAL;
	}


	if(json_get_string(cmd->j_command, "textname", &text_name, true) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing textname");
		return MOSQ_ERR_INVAL;
	}

	if(json_get_string(cmd->j_command, "textdescription", &text_description, true) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing textdescription");
		return MOSQ_ERR_INVAL;
	}

	client = dynsec_clients__find(data, username);
	if(client){
		mosquitto_control_command_reply(cmd, "Client already exists");
		return MOSQ_ERR_SUCCESS;
	}

	client = mosquitto_calloc(1, sizeof(struct dynsec__client) + username_len + 1);
	if(client == NULL){
		mosquitto_control_command_reply(cmd, "Internal error");
		return MOSQ_ERR_NOMEM;
	}
	strncpy(client->username, username, username_len);

	if(text_name){
		client->text_name = mosquitto_strdup(text_name);
		if(client->text_name == NULL){
			mosquitto_control_command_reply(cmd, "Internal error");
			client__free_item(data, client);
			return MOSQ_ERR_NOMEM;
		}
	}
	if(text_description){
		client->text_description = mosquitto_strdup(text_description);
		if(client->text_description == NULL){
			mosquitto_control_command_reply(cmd, "Internal error");
			client__free_item(data, client);
			return MOSQ_ERR_NOMEM;
		}
	}

	if(password){
		if(mosquitto_pw_new(&client->pw, MOSQ_PW_DEFAULT)
				|| mosquitto_pw_hash_encoded(client->pw, password)
				){

			mosquitto_control_command_reply(cmd, "Internal error");
			client__free_item(data, client);
			return MOSQ_ERR_NOMEM;
		}
	}
	if(clientid && strlen(clientid) > 0){
		client->clientid = mosquitto_strdup(clientid);
		if(client->clientid == NULL){
			mosquitto_control_command_reply(cmd, "Internal error");
			client__free_item(data, client);
			return MOSQ_ERR_NOMEM;
		}
	}

	rc = dynsec_rolelist__load_from_json(data, cmd->j_command, &client->rolelist);
	if(rc == MOSQ_ERR_SUCCESS || rc == ERR_LIST_NOT_FOUND){
	}else if(rc == MOSQ_ERR_NOT_FOUND){
		mosquitto_control_command_reply(cmd, "Role not found");
		client__free_item(data, client);
		return MOSQ_ERR_INVAL;
	}else{
		if(rc == MOSQ_ERR_INVAL){
			mosquitto_control_command_reply(cmd, "'roles' not an array or missing/invalid rolename");
		}else{
			mosquitto_control_command_reply(cmd, "Internal error");
		}
		client__free_item(data, client);
		return MOSQ_ERR_INVAL;
	}

	/* Must add user before groups, otherwise adding groups will fail */
	HASH_ADD_INORDER(hh, data->clients, username, username_len, client, client_cmp);

	j_groups = cJSON_GetObjectItem(cmd->j_command, "groups");
	if(j_groups && cJSON_IsArray(j_groups)){
		cJSON_ArrayForEach(j_group, j_groups){
			if(cJSON_IsObject(j_group)){
				const char *groupname;
				if(json_get_string(j_group, "groupname", &groupname, false) == MOSQ_ERR_SUCCESS){
					json_get_int(j_group, "priority", &priority, true, -1);
					if(priority > PRIORITY_MAX) priority = PRIORITY_MAX;
					rc = dynsec_groups__add_client(data, username, groupname, priority, false);
					if(rc == ERR_GROUP_NOT_FOUND){
						mosquitto_control_command_reply(cmd, "Group not found");
						client__free_item(data, client);
						return MOSQ_ERR_INVAL;
					}else if(rc != MOSQ_ERR_SUCCESS){
						mosquitto_control_command_reply(cmd, "Internal error");
						client__free_item(data, client);
						return MOSQ_ERR_INVAL;
					}
				}
			}
		}
	}

	dynsec__config_batch_save(data);

	mosquitto_control_command_reply(cmd, NULL);

	admin_clientid = mosquitto_client_id(cmd->client);
	admin_username = mosquitto_client_username(cmd->client);
	mosquitto_log_printf(MOSQ_LOG_INFO, "dynsec: %s/%s | createClient | username=%s | password=%s",
			admin_clientid, admin_username, username, password?"*****":"no password");

	return MOSQ_ERR_SUCCESS;
}


int dynsec_clients__process_delete(struct dynsec__data *data, struct mosquitto_control_cmd *cmd)
{
	const char *username;
	struct dynsec__client *client;
	const char *admin_clientid, *admin_username;

	if(json_get_string(cmd->j_command, "username", &username, false) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing username");
		return MOSQ_ERR_INVAL;
	}

	client = dynsec_clients__find(data, username);
	if(client){
		dynsec__remove_client_from_all_groups(data, username);
		client__remove_all_roles(client);
		client__free_item(data, client);
		dynsec__config_batch_save(data);
		mosquitto_control_command_reply(cmd, NULL);

		/* Enforce any changes */
		dynsec_kicklist__add(data, username);

		admin_clientid = mosquitto_client_id(cmd->client);
		admin_username = mosquitto_client_username(cmd->client);
		mosquitto_log_printf(MOSQ_LOG_INFO, "dynsec: %s/%s | deleteClient | username=%s",
				admin_clientid, admin_username, username);

		return MOSQ_ERR_SUCCESS;
	}else{
		mosquitto_control_command_reply(cmd, "Client not found");
		return MOSQ_ERR_SUCCESS;
	}
}

int dynsec_clients__process_disable(struct dynsec__data *data, struct mosquitto_control_cmd *cmd)
{
	const char *username;
	struct dynsec__client *client;
	const char *admin_clientid, *admin_username;

	if(json_get_string(cmd->j_command, "username", &username, false) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing username");
		return MOSQ_ERR_INVAL;
	}
	if(mosquitto_validate_utf8(username, (int)strlen(username)) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Username not valid UTF-8");
		return MOSQ_ERR_INVAL;
	}

	client = dynsec_clients__find(data, username);
	if(client == NULL){
		mosquitto_control_command_reply(cmd, "Client not found");
		return MOSQ_ERR_SUCCESS;
	}

	client->disabled = true;

	dynsec_kicklist__add(data, username);

	dynsec__config_batch_save(data);
	mosquitto_control_command_reply(cmd, NULL);

	admin_clientid = mosquitto_client_id(cmd->client);
	admin_username = mosquitto_client_username(cmd->client);
	mosquitto_log_printf(MOSQ_LOG_INFO, "dynsec: %s/%s | disableClient | username=%s",
			admin_clientid, admin_username, username);

	return MOSQ_ERR_SUCCESS;
}


int dynsec_clients__process_enable(struct dynsec__data *data, struct mosquitto_control_cmd *cmd)
{
	const char *username;
	struct dynsec__client *client;
	const char *admin_clientid, *admin_username;

	if(json_get_string(cmd->j_command, "username", &username, false) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing username");
		return MOSQ_ERR_INVAL;
	}
	if(mosquitto_validate_utf8(username, (int)strlen(username)) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Username not valid UTF-8");
		return MOSQ_ERR_INVAL;
	}

	client = dynsec_clients__find(data, username);
	if(client == NULL){
		mosquitto_control_command_reply(cmd, "Client not found");
		return MOSQ_ERR_SUCCESS;
	}

	client->disabled = false;

	dynsec__config_batch_save(data);
	mosquitto_control_command_reply(cmd, NULL);

	admin_clientid = mosquitto_client_id(cmd->client);
	admin_username = mosquitto_client_username(cmd->client);
	mosquitto_log_printf(MOSQ_LOG_INFO, "dynsec: %s/%s | enableClient | username=%s",
			admin_clientid, admin_username, username);

	return MOSQ_ERR_SUCCESS;
}


int dynsec_clients__process_set_id(struct dynsec__data *data, struct mosquitto_control_cmd *cmd)
{
	const char *username, *clientid;
	char *clientid_heap = NULL;
	struct dynsec__client *client;
	size_t slen;
	const char *admin_clientid, *admin_username;

	if(json_get_string(cmd->j_command, "username", &username, false) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing username");
		return MOSQ_ERR_INVAL;
	}
	if(mosquitto_validate_utf8(username, (int)strlen(username)) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Username not valid UTF-8");
		return MOSQ_ERR_INVAL;
	}

	if(json_get_string(cmd->j_command, "clientid", &clientid, true) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing client ID");
		return MOSQ_ERR_INVAL;
	}
	if(clientid){
		slen = strlen(clientid);
		if(mosquitto_validate_utf8(clientid, (int)slen) != MOSQ_ERR_SUCCESS){
			mosquitto_control_command_reply(cmd, "Client ID not valid UTF-8");
			return MOSQ_ERR_INVAL;
		}
		if(slen > 0){
			clientid_heap = mosquitto_strdup(clientid);
			if(clientid_heap == NULL){
				mosquitto_control_command_reply(cmd, "Internal error");
				return MOSQ_ERR_NOMEM;
			}
		}else{
			clientid_heap = NULL;
		}
	}

	client = dynsec_clients__find(data, username);
	if(client == NULL){
		mosquitto_free(clientid_heap);
		mosquitto_control_command_reply(cmd, "Client not found");
		return MOSQ_ERR_SUCCESS;
	}

	mosquitto_free(client->clientid);
	client->clientid = clientid_heap;

	dynsec__config_batch_save(data);
	mosquitto_control_command_reply(cmd, NULL);

	/* Enforce any changes */
	dynsec_kicklist__add(data, username);

	admin_clientid = mosquitto_client_id(cmd->client);
	admin_username = mosquitto_client_username(cmd->client);
	mosquitto_log_printf(MOSQ_LOG_INFO, "dynsec: %s/%s | setClientId | username=%s | clientid=%s",
			admin_clientid, admin_username, username, client->clientid);

	return MOSQ_ERR_SUCCESS;
}


static int client__set_password(struct dynsec__client *client, const char *password)
{
	if(!client->pw){
		if(mosquitto_pw_new(&client->pw, MOSQ_PW_DEFAULT)){
			return MOSQ_ERR_NOMEM;
		}
	}
	return mosquitto_pw_hash_encoded(client->pw, password);
}

int dynsec_clients__process_set_password(struct dynsec__data *data, struct mosquitto_control_cmd *cmd)
{
	const char *username, *password;
	struct dynsec__client *client;
	int rc;
	const char *admin_clientid, *admin_username;

	if(json_get_string(cmd->j_command, "username", &username, false) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing username");
		return MOSQ_ERR_INVAL;
	}
	if(mosquitto_validate_utf8(username, (int)strlen(username)) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Username not valid UTF-8");
		return MOSQ_ERR_INVAL;
	}

	if(json_get_string(cmd->j_command, "password", &password, false) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing password");
		return MOSQ_ERR_INVAL;
	}
	if(strlen(password) == 0){
		mosquitto_control_command_reply(cmd, "Empty password is not allowed");
		return MOSQ_ERR_INVAL;
	}

	client = dynsec_clients__find(data, username);
	if(client == NULL){
		mosquitto_control_command_reply(cmd, "Client not found");
		return MOSQ_ERR_SUCCESS;
	}
	rc = client__set_password(client, password);
	if(rc == MOSQ_ERR_SUCCESS){
		dynsec__config_batch_save(data);
		mosquitto_control_command_reply(cmd, NULL);

		/* Enforce any changes */
		dynsec_kicklist__add(data, username);

		admin_clientid = mosquitto_client_id(cmd->client);
		admin_username = mosquitto_client_username(cmd->client);
		mosquitto_log_printf(MOSQ_LOG_INFO, "dynsec: %s/%s | setClientPassword | username=%s | password=******",
				admin_clientid, admin_username, username);
	}else{
		mosquitto_control_command_reply(cmd, "Internal error");
	}
	return rc;
}


static void client__add_new_roles(struct dynsec__client *client, struct dynsec__rolelist *base_rolelist)
{
	struct dynsec__rolelist *rolelist, *rolelist_tmp;

	HASH_ITER(hh, base_rolelist, rolelist, rolelist_tmp){
		dynsec_rolelist__client_add(client, rolelist->role, rolelist->priority);
	}
}

static void client__remove_all_roles(struct dynsec__client *client)
{
	struct dynsec__rolelist *rolelist, *rolelist_tmp;

	HASH_ITER(hh, client->rolelist, rolelist, rolelist_tmp){
		dynsec_rolelist__client_remove(client, rolelist->role);
	}
}

int dynsec_clients__process_modify(struct dynsec__data *data, struct mosquitto_control_cmd *cmd)
{
	const char *username;
	char *clientid = NULL;
	const char *password = NULL;
	char *text_name = NULL, *text_description = NULL;
	bool have_clientid = false, have_text_name = false, have_text_description = false, have_rolelist = false, have_password = false;
	struct dynsec__client *client;
	struct dynsec__group *group;
	struct dynsec__rolelist *rolelist = NULL;
	const char *str;
	int rc;
	int priority;
	cJSON *j_group, *j_groups;
	const char *admin_clientid, *admin_username;

	if(json_get_string(cmd->j_command, "username", &username, false) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing username");
		return MOSQ_ERR_INVAL;
	}
	if(mosquitto_validate_utf8(username, (int)strlen(username)) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Username not valid UTF-8");
		return MOSQ_ERR_INVAL;
	}

	client = dynsec_clients__find(data, username);
	if(client == NULL){
		mosquitto_control_command_reply(cmd, "Client not found");
		return MOSQ_ERR_INVAL;
	}

	if(json_get_string(cmd->j_command, "clientid", &str, false) == MOSQ_ERR_SUCCESS){
		have_clientid = true;
		if(str && strlen(str) > 0){
			clientid = mosquitto_strdup(str);
			if(clientid == NULL){
				mosquitto_control_command_reply(cmd, "Internal error");
				rc = MOSQ_ERR_NOMEM;
				goto error;
			}
		}else{
			clientid = NULL;
		}
	}

	if(json_get_string(cmd->j_command, "password", &password, false) == MOSQ_ERR_SUCCESS){
		if(strlen(password) > 0){
			have_password = true;
		}
	}

	if(json_get_string(cmd->j_command, "textname", &str, false) == MOSQ_ERR_SUCCESS){
		have_text_name = true;
		text_name = mosquitto_strdup(str);
		if(text_name == NULL){
			mosquitto_control_command_reply(cmd, "Internal error");
			rc = MOSQ_ERR_NOMEM;
			goto error;
		}
	}

	if(json_get_string(cmd->j_command, "textdescription", &str, false) == MOSQ_ERR_SUCCESS){
		have_text_description = true;
		text_description = mosquitto_strdup(str);
		if(text_description == NULL){
			mosquitto_control_command_reply(cmd, "Internal error");
			rc = MOSQ_ERR_NOMEM;
			goto error;
		}
	}

	rc = dynsec_rolelist__load_from_json(data, cmd->j_command, &rolelist);
	if(rc == MOSQ_ERR_SUCCESS){
		have_rolelist = true;
	}else if(rc == ERR_LIST_NOT_FOUND){
		/* There was no list in the JSON, so no modification */
	}else if(rc == MOSQ_ERR_NOT_FOUND){
		mosquitto_control_command_reply(cmd, "Role not found");
		rc = MOSQ_ERR_INVAL;
		goto error;
	}else{
		if(rc == MOSQ_ERR_INVAL){
			mosquitto_control_command_reply(cmd, "'roles' not an array or missing/invalid rolename");
		}else{
			mosquitto_control_command_reply(cmd, "Internal error");
		}
		rc = MOSQ_ERR_INVAL;
		goto error;
	}

	j_groups = cJSON_GetObjectItem(cmd->j_command, "groups");
	if(j_groups && cJSON_IsArray(j_groups)){
		/* Iterate through list to check all groups are valid */
		cJSON_ArrayForEach(j_group, j_groups){
			if(cJSON_IsObject(j_group)){
				const char *groupname;
				if(json_get_string(j_group, "groupname", &groupname, false) == MOSQ_ERR_SUCCESS){
					group = dynsec_groups__find(data, groupname);
					if(group == NULL){
						mosquitto_control_command_reply(cmd, "'groups' contains an object with a 'groupname' that does not exist");
						rc = MOSQ_ERR_INVAL;
						goto error;
					}
				}else{
					mosquitto_control_command_reply(cmd, "'groups' contains an object with an invalid 'groupname'");
					rc = MOSQ_ERR_INVAL;
					goto error;
				}
			}
		}

		dynsec__remove_client_from_all_groups(data, username);
		cJSON_ArrayForEach(j_group, j_groups){
			if(cJSON_IsObject(j_group)){
				const char *groupname;
				if(json_get_string(j_group, "groupname", &groupname, false) == MOSQ_ERR_SUCCESS){
					json_get_int(j_group, "priority", &priority, true, -1);
					if(priority > PRIORITY_MAX) priority = PRIORITY_MAX;
					dynsec_groups__add_client(data, username, groupname, priority, false);
				}
			}
		}
	}

	if(have_password){
		/* FIXME - This is the one call that will result in modification on internal error - note that groups have already been modified */
		rc = client__set_password(client, password);
		if(rc != MOSQ_ERR_SUCCESS){
			mosquitto_control_command_reply(cmd, "Internal error");
			mosquitto_kick_client_by_username(username, false);
			/* If this fails we have the situation that the password is set as
			 * invalid, but the config isn't saved, so restarting the broker
			 * *now* will mean the client can log in again. This might be
			 * "good", but is inconsistent, so save the config to be
			 * consistent. */
			dynsec__config_batch_save(data);
			rc = MOSQ_ERR_NOMEM;
			goto error;
		}
	}

	if(have_clientid){
		mosquitto_free(client->clientid);
		client->clientid = clientid;
	}

	if(have_text_name){
		mosquitto_free(client->text_name);
		client->text_name = text_name;
	}

	if(have_text_description){
		mosquitto_free(client->text_description);
		client->text_description = text_description;
	}

	if(have_rolelist){
		client__remove_all_roles(client);
		client__add_new_roles(client, rolelist);
		dynsec_rolelist__cleanup(&rolelist);
	}

	dynsec__config_batch_save(data);
	mosquitto_control_command_reply(cmd, NULL);

	/* Enforce any changes */
	dynsec_kicklist__add(data, username);

	admin_clientid = mosquitto_client_id(cmd->client);
	admin_username = mosquitto_client_username(cmd->client);
	mosquitto_log_printf(MOSQ_LOG_INFO, "dynsec: %s/%s | modifyClient | username=%s",
			admin_clientid, admin_username, username);
	return MOSQ_ERR_SUCCESS;
error:
	mosquitto_free(clientid);
	mosquitto_free(text_name);
	mosquitto_free(text_description);
	dynsec_rolelist__cleanup(&rolelist);
	return rc;
}


static int dynsec__remove_client_from_all_groups(struct dynsec__data *data, const char *username)
{
	struct dynsec__grouplist *grouplist, *grouplist_tmp;
	struct dynsec__client *client;

	client = dynsec_clients__find(data, username);
	if(client){
		HASH_ITER(hh, client->grouplist, grouplist, grouplist_tmp){
			dynsec_groups__remove_client(data, username, grouplist->group->groupname, false);
		}
	}

	return MOSQ_ERR_SUCCESS;
}


static int dynsec__add_client_address(const struct mosquitto *client, void *context_ptr)
{
	struct connection_array_context *functor_context = (struct connection_array_context*)context_ptr;
	const char *username = mosquitto_client_username(client);

	if((username == NULL && functor_context->username == NULL)
			|| (username && functor_context->username && !strcmp(functor_context->username, username))){

		cJSON *j_connection = cJSON_CreateObject();
		const char *address;
		if(!j_connection){
			return MOSQ_ERR_NOMEM;
		}
		if((address = mosquitto_client_address(client)) && !cJSON_AddStringToObject(j_connection, "address", address)){
			cJSON_Delete(j_connection);
			return MOSQ_ERR_NOMEM;
		}
		cJSON_AddItemToArray(functor_context->j_connections, j_connection);
	}
	return MOSQ_ERR_SUCCESS;
}


static cJSON *dynsec_connections__all_to_json(const char *username, const char *clientid)
{
	struct connection_array_context functor_context = { username, cJSON_CreateArray()};

	if(functor_context.j_connections == NULL){
		return NULL;
	}

	if(clientid){
		const struct mosquitto *client = mosquitto_client(clientid);
		if(client && dynsec__add_client_address(client, &functor_context) != MOSQ_ERR_SUCCESS){
			cJSON_Delete(functor_context.j_connections);
			return NULL;
		}
	}else{
		if(mosquitto_apply_on_all_clients(&dynsec__add_client_address, &functor_context) != MOSQ_ERR_SUCCESS){
			cJSON_Delete(functor_context.j_connections);
			return NULL;
		}
	}
	return functor_context.j_connections;
}


static cJSON *add_client_to_json(struct dynsec__client *client, bool verbose)
{
	cJSON *j_client = NULL;

	if(verbose){
		cJSON *j_groups, *j_roles, *j_connections;

		j_client = cJSON_CreateObject();
		if(j_client == NULL){
			return NULL;
		}

		if(cJSON_AddStringToObject(j_client, "username", client->username) == NULL
				|| (client->clientid && cJSON_AddStringToObject(j_client, "clientid", client->clientid) == NULL)
				|| (client->text_name && cJSON_AddStringToObject(j_client, "textname", client->text_name) == NULL)
				|| (client->text_description && cJSON_AddStringToObject(j_client, "textdescription", client->text_description) == NULL)
				|| (client->disabled && cJSON_AddBoolToObject(j_client, "disabled", client->disabled) == NULL)
				){

			cJSON_Delete(j_client);
			return NULL;
		}

		j_roles = dynsec_rolelist__all_to_json(client->rolelist);
		if(j_roles == NULL){
			cJSON_Delete(j_client);
			return NULL;
		}
		cJSON_AddItemToObject(j_client, "roles", j_roles);

		j_groups = dynsec_grouplist__all_to_json(client->grouplist);
		if(j_groups == NULL){
			cJSON_Delete(j_client);
			return NULL;
		}
		cJSON_AddItemToObject(j_client, "groups", j_groups);

		j_connections = dynsec_connections__all_to_json(client->username, client->clientid);
		if(j_connections == NULL){
			cJSON_Delete(j_client);
			return NULL;
		}
		cJSON_AddItemToObject(j_client, "connections", j_connections);
	}else{
		j_client = cJSON_CreateString(client->username);
		if(j_client == NULL){
			return NULL;
		}
	}
	return j_client;
}


int dynsec_clients__process_get(struct dynsec__data *data, struct mosquitto_control_cmd *cmd)
{
	const char *username;
	struct dynsec__client *client;
	cJSON *tree, *j_client, *j_data;
	const char *admin_clientid, *admin_username;

	if(json_get_string(cmd->j_command, "username", &username, false) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing username");
		return MOSQ_ERR_INVAL;
	}
	if(mosquitto_validate_utf8(username, (int)strlen(username)) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Username not valid UTF-8");
		return MOSQ_ERR_INVAL;
	}

	client = dynsec_clients__find(data, username);
	if(client == NULL){
		mosquitto_control_command_reply(cmd, "Client not found");
		return MOSQ_ERR_SUCCESS;
	}

	tree = cJSON_CreateObject();
	if(tree == NULL){
		mosquitto_control_command_reply(cmd, "Internal error");
		return MOSQ_ERR_NOMEM;
	}

	if(cJSON_AddStringToObject(tree, "command", "getClient") == NULL
			|| (j_data = cJSON_AddObjectToObject(tree, "data")) == NULL
			|| (cmd->correlation_data && cJSON_AddStringToObject(tree, "correlationData", cmd->correlation_data) == NULL)
			){

		cJSON_Delete(tree);
		mosquitto_control_command_reply(cmd, "Internal error");
		return MOSQ_ERR_NOMEM;
	}

	j_client = add_client_to_json(client, true);
	if(j_client == NULL){
		cJSON_Delete(tree);
		mosquitto_control_command_reply(cmd, "Internal error");
		return MOSQ_ERR_NOMEM;
	}
	cJSON_AddItemToObject(j_data, "client", j_client);
	cJSON_AddItemToArray(cmd->j_responses, tree);

	admin_clientid = mosquitto_client_id(cmd->client);
	admin_username = mosquitto_client_username(cmd->client);
	mosquitto_log_printf(MOSQ_LOG_INFO, "dynsec: %s/%s | getClient | username=%s",
			admin_clientid, admin_username, username);

	return MOSQ_ERR_SUCCESS;
}


int dynsec_clients__process_list(struct dynsec__data *data, struct mosquitto_control_cmd *cmd)
{
	bool verbose;
	struct dynsec__client *client, *client_tmp;
	cJSON *tree, *j_clients, *j_client, *j_data;
	int i, count, offset;
	const char *admin_clientid, *admin_username;

	json_get_bool(cmd->j_command, "verbose", &verbose, true, false);
	json_get_int(cmd->j_command, "count", &count, true, -1);
	json_get_int(cmd->j_command, "offset", &offset, true, 0);

	tree = cJSON_CreateObject();
	if(tree == NULL){
		mosquitto_control_command_reply(cmd, "Internal error");
		return MOSQ_ERR_NOMEM;
	}

	if(cJSON_AddStringToObject(tree, "command", "listClients") == NULL
			|| (j_data = cJSON_AddObjectToObject(tree, "data")) == NULL
			|| cJSON_AddIntToObject(j_data, "totalCount", (int)HASH_CNT(hh, data->clients)) == NULL
			|| (j_clients = cJSON_AddArrayToObject(j_data, "clients")) == NULL
			|| (cmd->correlation_data && cJSON_AddStringToObject(tree, "correlationData", cmd->correlation_data) == NULL)
			){

		cJSON_Delete(tree);
		mosquitto_control_command_reply(cmd, "Internal error");
		return MOSQ_ERR_NOMEM;
	}

	i = 0;
	HASH_ITER(hh, data->clients, client, client_tmp){
		if(i>=offset){
			j_client = add_client_to_json(client, verbose);
			if(j_client == NULL){
				cJSON_Delete(tree);
				mosquitto_control_command_reply(cmd, "Internal error");
				return MOSQ_ERR_NOMEM;
			}
			cJSON_AddItemToArray(j_clients, j_client);

			if(count >= 0){
				count--;
				if(count <= 0){
					break;
				}
			}
		}
		i++;
	}
	cJSON_AddItemToArray(cmd->j_responses, tree);

	admin_clientid = mosquitto_client_id(cmd->client);
	admin_username = mosquitto_client_username(cmd->client);
	mosquitto_log_printf(MOSQ_LOG_INFO, "dynsec: %s/%s | listClients | verbose=%s | count=%d | offset=%d",
			admin_clientid, admin_username, verbose?"true":"false", count, offset);

	return MOSQ_ERR_SUCCESS;
}


int dynsec_clients__process_add_role(struct dynsec__data *data, struct mosquitto_control_cmd *cmd)
{
	const char *username, *rolename;
	struct dynsec__client *client;
	struct dynsec__role *role;
	int priority;
	const char *admin_clientid, *admin_username;

	if(json_get_string(cmd->j_command, "username", &username, false) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing username");
		return MOSQ_ERR_INVAL;
	}
	if(mosquitto_validate_utf8(username, (int)strlen(username)) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Username not valid UTF-8");
		return MOSQ_ERR_INVAL;
	}

	if(json_get_string(cmd->j_command, "rolename", &rolename, false) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing rolename");
		return MOSQ_ERR_INVAL;
	}
	if(mosquitto_validate_utf8(rolename, (int)strlen(rolename)) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Role name not valid UTF-8");
		return MOSQ_ERR_INVAL;
	}
	json_get_int(cmd->j_command, "priority", &priority, true, -1);
	if(priority > PRIORITY_MAX) priority = PRIORITY_MAX;

	client = dynsec_clients__find(data, username);
	if(client == NULL){
		mosquitto_control_command_reply(cmd, "Client not found");
		return MOSQ_ERR_SUCCESS;
	}

	role = dynsec_roles__find(data, rolename);
	if(role == NULL){
		mosquitto_control_command_reply(cmd, "Role not found");
		return MOSQ_ERR_SUCCESS;
	}

	if(dynsec_rolelist__client_add(client, role, priority) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Internal error");
		return MOSQ_ERR_UNKNOWN;
	}
	dynsec__config_batch_save(data);
	mosquitto_control_command_reply(cmd, NULL);

	/* Enforce any changes */
	dynsec_kicklist__add(data, username);

	admin_clientid = mosquitto_client_id(cmd->client);
	admin_username = mosquitto_client_username(cmd->client);
	mosquitto_log_printf(MOSQ_LOG_INFO, "dynsec: %s/%s | addClientRole | username=%s | rolename=%s | priority=%d",
			admin_clientid, admin_username, username, rolename, priority);

	return MOSQ_ERR_SUCCESS;
}


int dynsec_clients__process_remove_role(struct dynsec__data *data, struct mosquitto_control_cmd *cmd)
{
	const char *username, *rolename;
	struct dynsec__client *client;
	struct dynsec__role *role;
	const char *admin_clientid, *admin_username;

	if(json_get_string(cmd->j_command, "username", &username, false) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing username");
		return MOSQ_ERR_INVAL;
	}
	if(mosquitto_validate_utf8(username, (int)strlen(username)) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Username not valid UTF-8");
		return MOSQ_ERR_INVAL;
	}

	if(json_get_string(cmd->j_command, "rolename", &rolename, false) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Invalid/missing rolename");
		return MOSQ_ERR_INVAL;
	}
	if(mosquitto_validate_utf8(rolename, (int)strlen(rolename)) != MOSQ_ERR_SUCCESS){
		mosquitto_control_command_reply(cmd, "Role name not valid UTF-8");
		return MOSQ_ERR_INVAL;
	}


	client = dynsec_clients__find(data, username);
	if(client == NULL){
		mosquitto_control_command_reply(cmd, "Client not found");
		return MOSQ_ERR_SUCCESS;
	}

	role = dynsec_roles__find(data, rolename);
	if(role == NULL){
		mosquitto_control_command_reply(cmd, "Role not found");
		return MOSQ_ERR_SUCCESS;
	}

	dynsec_rolelist__client_remove(client, role);
	dynsec__config_batch_save(data);
	mosquitto_control_command_reply(cmd, NULL);

	/* Enforce any changes */
	dynsec_kicklist__add(data, username);

	admin_clientid = mosquitto_client_id(cmd->client);
	admin_username = mosquitto_client_username(cmd->client);
	mosquitto_log_printf(MOSQ_LOG_INFO, "dynsec: %s/%s | removeClientRole | username=%s | rolename=%s",
			admin_clientid, admin_username, username, rolename);

	return MOSQ_ERR_SUCCESS;
}
