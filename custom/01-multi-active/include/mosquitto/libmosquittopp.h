/*
Copyright (c) 2010-2021 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License 2.0
and Eclipse Distribution License v1.0 which accompany this distribution.

The Eclipse Public License is available at
   https://www.eclipse.org/legal/epl-2.0/
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.

Contributors:
   Roger Light - initial implementation and documentation.
*/

#ifndef MOSQUITTO_LIBMOSQUITTOPP_H
#define MOSQUITTO_LIBMOSQUITTOPP_H

#if defined(_WIN32) && !defined(LIBMOSQUITTO_STATIC)
#	ifdef mosquittopp_EXPORTS
#		define mosqpp_EXPORT  __declspec(dllexport)
#	else
#		define mosqpp_EXPORT  __declspec(dllimport)
#	endif
#else
#	define mosqpp_EXPORT
#endif

#include <cstdlib>
#include <mosquitto.h>
#include <time.h>

namespace mosqpp {


mosqpp_EXPORT const char * strerror(int mosq_errno);
mosqpp_EXPORT const char * connack_string(int connack_code);
mosqpp_EXPORT const char * reason_string(int reason_code);
mosqpp_EXPORT int sub_topic_tokenise(const char *subtopic, char ***topics, int *count);
mosqpp_EXPORT int sub_topic_tokens_free(char ***topics, int count);
mosqpp_EXPORT int lib_version(int *major, int *minor, int *revision);
mosqpp_EXPORT int lib_init();
mosqpp_EXPORT int lib_cleanup();
mosqpp_EXPORT int topic_matches_sub(const char *sub, const char *topic, bool *result);
mosqpp_EXPORT int topic_matches_sub_with_pattern(const char *sub, const char *topic, const char *clientid, const char *username, bool *result);
mosqpp_EXPORT int sub_matches_acl(const char *acl, const char *sub, bool *result);
mosqpp_EXPORT int sub_matches_acl_with_pattern(const char *acl, const char *sub, const char *clientid, const char *username, bool *result);
mosqpp_EXPORT int validate_utf8(const char *str, int len);
mosqpp_EXPORT int subscribe_simple(
		struct mosquitto_message **messages,
		int msg_count,
		bool retained,
		const char *topic,
		int qos=0,
		const char *host="localhost",
		int port=1883,
		const char *clientid=NULL,
		int keepalive=60,
		bool clean_session=true,
		const char *username=NULL,
		const char *password=NULL,
		const struct libmosquitto_will *will=NULL,
		const struct libmosquitto_tls *tls=NULL);

mosqpp_EXPORT int subscribe_callback(
		int (*callback)(struct mosquitto *, void *, const struct mosquitto_message *),
		void *userdata,
		const char *topic,
		int qos=0,
		const char *host="localhost",
		int port=1883,
		const char *clientid=NULL,
		int keepalive=60,
		bool clean_session=true,
		const char *username=NULL,
		const char *password=NULL,
		const struct libmosquitto_will *will=NULL,
		const struct libmosquitto_tls *tls=NULL);

mosqpp_EXPORT int property_check_command(int command, int identifier);
mosqpp_EXPORT int property_check_all(int command, const mosquitto_property *properties);

/*
 * Class: mosquittopp
 *
 * A mosquitto client class. This is a C++ wrapper class for the mosquitto C
 * library. Please see mosquitto.h for details of the functions.
 */
class mosqpp_EXPORT mosquittopp {
	private:
		struct mosquitto *m_mosq;
	public:
		mosquittopp(const char *id=NULL, bool clean_session=true);
		virtual ~mosquittopp();

		int reinitialise(const char *id, bool clean_session);
		int socket();
		int will_set(const char *topic, int payloadlen=0, const void *payload=NULL, int qos=0, bool retain=false);
		int will_set_v5(const char *topic, int payloadlen=0, const void *payload=NULL, int qos=0, bool retain=false, mosquitto_property *properties=NULL);
		int will_clear();
		int username_pw_set(const char *username, const char *password=NULL);
		int connect(const char *host, int port=1883, int keepalive=60);
		int connect(const char *host, int port, int keepalive, const char *bind_address);
		int connect_v5(const char *host, int port, int keepalive, const char *bind_address, const mosquitto_property *properties);
		int connect_async(const char *host, int port=1883, int keepalive=60);
		int connect_async(const char *host, int port, int keepalive, const char *bind_address);
		int reconnect();
		int reconnect_async();
		int disconnect();
		int disconnect_v5(int reason_code, const mosquitto_property *properties);
		int publish(int *mid, const char *topic, int payloadlen=0, const void *payload=NULL, int qos=0, bool retain=false);
		int publish_v5(int *mid, const char *topic, int payloadlen=0, const void *payload=NULL, int qos=0, bool retain=false, const mosquitto_property *properties=NULL);
		int subscribe(int *mid, const char *sub, int qos=0);
		int subscribe_v5(int *mid, const char *sub, int qos=0, int options=0, const mosquitto_property *properties=NULL);
		int unsubscribe(int *mid, const char *sub);
		int unsubscribe_v5(int *mid, const char *sub, const mosquitto_property *properties);
		void reconnect_delay_set(unsigned int reconnect_delay, unsigned int reconnect_delay_max, bool reconnect_exponential_backoff);
		int max_inflight_messages_set(unsigned int max_inflight_messages);
		void message_retry_set(unsigned int message_retry);
		void user_data_set(void *userdata);
		int tls_set(const char *cafile, const char *capath=NULL, const char *certfile=NULL, const char *keyfile=NULL, int (*pw_callback)(char *buf, int size, int rwflag, void *userdata)=NULL);
		int ext_auth_continue(const char *auth_method, uint16_t auth_data_len=0, const void *auth_data=NULL, const mosquitto_property *properties=NULL);
		int tls_opts_set(int cert_reqs, const char *tls_version=NULL, const char *ciphers=NULL);
		int tls_insecure_set(bool value);
		int tls_psk_set(const char *psk, const char *identity, const char *ciphers=NULL);
		int opts_set(enum mosq_opt_t option, void *value);
		int int_option(enum mosq_opt_t option, int value);
		int string_option(enum mosq_opt_t option, const char *value);
		int void_option(enum mosq_opt_t option, void *value);

		int loop(int timeout=-1, int max_packets=1);
		int loop_misc();
		int loop_read(int max_packets=1);
		int loop_write(int max_packets=1);
		int loop_forever(int timeout=-1, int max_packets=1);
		int loop_start();
		int loop_stop(bool force=false);
		bool want_write();
		int threaded_set(bool threaded=true);
		int socks5_set(const char *host, int port=1080, const char *username=NULL, const char *password=NULL);

		// names in the functions commented to prevent unused parameter warning
		virtual void on_pre_connect() {return;}
		virtual void on_connect(int /*rc*/) {return;}
		virtual void on_connect_with_flags(int /*rc*/, int /*flags*/) {return;}
		virtual void on_connect_v5(int /*rc*/, int /*flags*/, const mosquitto_property * /*props*/) {return;}
		virtual void on_disconnect(int /*rc*/) {return;}
		virtual void on_disconnect_v5(int /*rc*/, const mosquitto_property * /*props*/) {return;}
		virtual void on_publish(int /*mid*/) {return;}
		virtual void on_publish_v5(int /*mid*/, int /*reason_code*/, const mosquitto_property * /*props*/) {return;}
		virtual void on_message(const struct mosquitto_message * /*message*/) {return;}
		virtual void on_message_v5(const struct mosquitto_message * /*message*/, const mosquitto_property * /*props*/) {return;}
		virtual void on_subscribe(int /*mid*/, int /*qos_count*/, const int * /*granted_qos*/) {return;}
		virtual void on_subscribe_v5(int /*mid*/, int /*qos_count*/, const int * /*granted_qos*/, const mosquitto_property * /*props*/) {return;}
		virtual void on_unsubscribe(int /*mid*/) {return;}
		virtual void on_unsubscribe_v5(int /*mid*/, const mosquitto_property * /*props*/) {return;}
		virtual void on_log(int /*level*/, const char * /*str*/) {return;}
		virtual void on_error() {return;}
		virtual int on_ext_auth(const char * /*auth_method*/, uint16_t /*auth_data_len*/, const void * /*auth_data*/, const mosquitto_property * /*props*/) {return MOSQ_ERR_AUTH;}
};

}
#endif
