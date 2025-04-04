#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <mosquitto.h>

#define UNUSED(A) (void)(A)

static int run = -1;
static int proto_ver;

#ifndef UNUSED
#  define UNUSED(A) (void)(A)
#endif

#ifdef _WIN32
#include <windows.h>

static BOOL WINAPI signal_handler(DWORD type)
{
    switch(type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            run = 0;
            return TRUE;
        default:
            return FALSE;
    }
}

static int setup_signal_handler(void)
{
    if(!SetConsoleCtrlHandler(signal_handler, TRUE)) {
        fprintf(stderr, "Error: Couldn't setup signal handler\n");
        return 1;
    }
    return 0;
}

#else
#include <signal.h>

static void handle_sigterm(int signal)
{
    run = 0;
}

static int setup_signal_handler(void)
{
    struct sigaction act = { 0 };
    act.sa_handler = handle_sigterm;
    act.sa_flags = 0;

    if(sigaction(SIGTERM, &act, NULL) < 0) {
        fprintf(stderr, "Error: Couldn't setup signal handler\n");
        return 1;
    }
    return 0;
}
#endif




// static void signal_handler(int s)
// {
// 	UNUSED(s);
// 	run = 0;
// }

static void prop_test(const mosquitto_property *props)
{
	mosquitto_property *dest = NULL;
	int rc;

	rc = mosquitto_property_copy_all(&dest, props);
	if(rc){
		printf("bad prop_test: %s\n", mosquitto_strerror(rc));
		exit(1);
	}
	mosquitto_property_free_all(&dest);
}

static void msg_test(const struct mosquitto_message *msg)
{
	struct mosquitto_message dest;
	int rc;

	memset(&dest, 0, sizeof(dest));
	rc = mosquitto_message_copy(&dest, msg);
	if(rc){
		printf("bad msg_test: %s\n", mosquitto_strerror(rc));
		exit(1);
	}
	mosquitto_message_free_contents(&dest);
}

static void on_pre_connect(struct mosquitto *mosq, void *obj)
{
	UNUSED(mosq);
	UNUSED(obj);
}

static void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
	char *command = obj;
	int mid;
	mosquitto_property *props = NULL;

	UNUSED(mosq);
	UNUSED(obj);

	if(command){
		if(proto_ver == 5){
			if(!strncmp(command, "subscribe", strlen("subscribe"))){
				rc = mosquitto_property_add_varint(&props, MQTT_PROP_SUBSCRIPTION_IDENTIFIER, 268435455);
				if(rc){
					printf("bad on_connect prop add: %s\n", mosquitto_strerror(rc));
					goto error;
				}
				rc = mosquitto_property_add_string_pair(&props, MQTT_PROP_USER_PROPERTY, "key", "value");
				if(rc){
					printf("bad on_connect prop add: %s\n", mosquitto_strerror(rc));
					goto error;
				}
			}else if(!strncmp(command, "unsubscribe", strlen("unsubscribe"))){
				rc = mosquitto_property_add_string_pair(&props, MQTT_PROP_USER_PROPERTY, "key", "value");
				if(rc){
					printf("bad on_connect prop add: %s\n", mosquitto_strerror(rc));
					goto error;
				}
			}else if(!strncmp(command, "publish", strlen("publish"))){
				rc = mosquitto_property_add_byte(&props, MQTT_PROP_PAYLOAD_FORMAT_INDICATOR, 1);
				if(rc){
					printf("bad on_connect prop add: %s\n", mosquitto_strerror(rc));
					goto error;
				}
				rc = mosquitto_property_add_int32(&props, MQTT_PROP_MESSAGE_EXPIRY_INTERVAL, UINT32_MAX);
				if(rc){
					printf("bad on_connect prop add: %s\n", mosquitto_strerror(rc));
					goto error;
				}
				rc = mosquitto_property_add_int16(&props, MQTT_PROP_TOPIC_ALIAS, UINT16_MAX);
				if(rc){
					printf("bad on_connect prop add: %s\n", mosquitto_strerror(rc));
					goto error;
				}
				rc = mosquitto_property_add_string(&props, MQTT_PROP_RESPONSE_TOPIC, "response/topic");
				if(rc){
					printf("bad on_connect prop add: %s\n", mosquitto_strerror(rc));
					goto error;
				}
				rc = mosquitto_property_add_binary(&props, MQTT_PROP_CORRELATION_DATA, "7deac5c5-8802-44ff-86ce-11479f337419", strlen("7deac5c5-8802-44ff-86ce-11479f337419"));
				if(rc){
					printf("bad on_connect prop add: %s\n", mosquitto_strerror(rc));
					goto error;
				}
				rc = mosquitto_property_add_string(&props, MQTT_PROP_CONTENT_TYPE, "text/plain");
				if(rc){
					printf("bad on_connect prop add: %s\n", mosquitto_strerror(rc));
					goto error;
				}
				rc = mosquitto_property_add_string_pair(&props, MQTT_PROP_USER_PROPERTY, "key", "value");
				if(rc){
					printf("bad on_connect prop add: %s\n", mosquitto_strerror(rc));
					goto error;
				}
			}
		}

		if(!strcmp(command, "subscribe-2")){
			rc = mosquitto_subscribe_v5(mosq, &mid, "test/subscribe", 2, 0, props);
			if(rc || mid != 1){
				printf("bad on_connect subscribe-2 %d || %d\n", rc, mid);
				goto error;
			}
		}else if(!strcmp(command, "subscribe-1")){
			rc = mosquitto_subscribe_v5(mosq, &mid, "test/subscribe", 1, 0, props);
			if(rc || mid != 1){
				printf("bad on_connect subscribe-1 %d || %d\n", rc, mid);
				goto error;
			}
		}else if(!strcmp(command, "subscribe-0")){
			rc = mosquitto_subscribe_v5(mosq, &mid, "test/subscribe", 0, 0, props);
			if(rc || mid != 1){
				printf("bad on_connect subscribe-0 %d || %d\n", rc, mid);
				goto error;
			}
		}else if(!strcmp(command, "subscribe-multiple")){
			char *subs[] = {"test/subscribe1", "test/subscribe2"};
			rc = mosquitto_subscribe_multiple(mosq, &mid, 2, subs, 2, 0, props);
			if(rc || mid != 1){
				printf("bad on_connect subscribe-multiple %d || %d\n", rc, mid);
				goto error;
			}
		}else if(!strcmp(command, "unsubscribe")){
			rc = mosquitto_unsubscribe_v5(mosq, &mid, "test/subscribe", props);
			if(rc || mid != 1){
				printf("bad on_connect unsubscribe %d || %d\n", rc, mid);
				goto error;
			}
		}else if(!strcmp(command, "unsubscribe-multiple")){
			char *subs[] = {"test/subscribe1", "test/subscribe2"};
			rc = mosquitto_unsubscribe_multiple(mosq, &mid, 2, subs, props);
			if(rc || mid != 1){
				printf("bad on_connect unsubscribe-multiple %d || %d\n", rc, mid);
				goto error;
			}
		}else if(!strcmp(command, "publish-2")){
			rc = mosquitto_publish_v5(mosq, &mid, "test/publish", strlen("message"), "message", 2, 0, props);
			if(rc || mid != 1){
				printf("bad on_connect publish-2 %d || %d\n", rc, mid);
				goto error;
			}
		}else if(!strcmp(command, "publish-1")){
			rc = mosquitto_publish_v5(mosq, &mid, "test/publish", strlen("message"), "message", 1, 0, props);
			if(rc || mid != 1){
				printf("bad on_connect publish-1 %d || %d\n", rc, mid);
				goto error;
			}
		}else if(!strcmp(command, "publish-0")){
			rc = mosquitto_publish_v5(mosq, &mid, "test/publish", strlen("message"), "message", 0, 0, props);
			if(rc || mid != 1){
				printf("bad on_connect publish-0 %d || %d\n", rc, mid);
				goto error;
			}
		}else{
			printf("bad on_connect command '%s'\n", command);
			goto error;
		}
	}

	mosquitto_property_free_all(&props);
	return;

error:
	mosquitto_property_free_all(&props);
	exit(1);
}

static void on_connect_with_flags(struct mosquitto *mosq, void *obj, int rc, int flags)
{
	UNUSED(mosq);
	UNUSED(obj);
	UNUSED(rc);
	UNUSED(flags);
}

static void on_connect_v5(struct mosquitto *mosq, void *obj, int rc, int flags, const mosquitto_property *props)
{
	UNUSED(mosq);
	UNUSED(obj);
	UNUSED(rc);
	UNUSED(flags);

	prop_test(props);
}

static void on_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
	UNUSED(mosq);
	UNUSED(obj);

	run = rc;
}

static void on_disconnect_v5(struct mosquitto *mosq, void *obj, int rc, const mosquitto_property *props)
{
	UNUSED(mosq);
	UNUSED(obj);

	prop_test(props);

	run = rc;
}

static void on_publish(struct mosquitto *mosq, void *obj, int mid)
{
	UNUSED(mosq);
	UNUSED(obj);
	UNUSED(mid);
}

static void on_publish_v5(struct mosquitto *mosq, void *obj, int mid, int reason_code, const mosquitto_property *props)
{
	UNUSED(mosq);
	UNUSED(obj);
	UNUSED(mid);
	UNUSED(reason_code);

	prop_test(props);
}

static void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
	UNUSED(mosq);
	UNUSED(obj);

	msg_test(msg);
}

static void on_message_v5(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg, const mosquitto_property *props)
{
	UNUSED(mosq);
	UNUSED(obj);

	msg_test(msg);
	prop_test(props);
}

static void on_subscribe(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
	UNUSED(mosq);
	UNUSED(mid);

	int tot = 0;

	UNUSED(mosq);
	UNUSED(obj);
	UNUSED(mid);

	for(int i=0; i<qos_count; i++){
		tot += granted_qos[i];
	}

	(void)tot;
}

static void on_subscribe_v5(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos, const mosquitto_property *props)
{
	UNUSED(mosq);
	UNUSED(mid);

	int tot = 0;

	UNUSED(mosq);
	UNUSED(obj);
	UNUSED(mid);

	for(int i=0; i<qos_count; i++){
		tot += granted_qos[i];
	}
	prop_test(props);

	(void)tot;
}

static void on_unsubscribe(struct mosquitto *mosq, void *obj, int mid)
{
	UNUSED(mosq);
	UNUSED(obj);
	UNUSED(mid);
}

static void on_unsubscribe_v5(struct mosquitto *mosq, void *obj, int mid, const mosquitto_property *props)
{
	UNUSED(mosq);
	UNUSED(obj);
	UNUSED(mid);

	prop_test(props);
}

static void on_unsubscribe2_v5(struct mosquitto *mosq, void *obj, int mid, int reason_code_count, const int *reason_codes, const mosquitto_property *props)
{
	UNUSED(mosq);
	UNUSED(obj);
	UNUSED(mid);

	int sum = 0;
	prop_test(props);
	for(int i=0; i<reason_code_count; i++){
		sum += reason_codes[i];
	}
	if(sum < 0){
		/* This is a "fake" condition to stop the above check being optimised out */
		exit(1);
	}
}

static void on_log(struct mosquitto *mosq, void *obj, int level, const char *str)
{
	UNUSED(mosq);
	UNUSED(obj);
	UNUSED(level);

	if(str == NULL){
		printf("bad on_log\n");
		exit(1);
	}
	size_t i = strlen(str);
	if (i == SIZE_MAX){
		printf("too large on_log\n");
		exit(1);
	}
}


// static void setup_signal_handler(void)
// {
// 	struct sigaction act = { 0 };

// 	act.sa_handler = &signal_handler;
// 	if(sigaction(SIGTERM, &act, NULL) < 0) {
// 		exit(1);
// 	}
// }

int main(int argc, char *argv[])
{
	int rc;
	struct mosquitto *mosq;
	int port;
	bool clean_start;
	char *command = NULL;

	if(argc < 4){
		return 1;
	}
	setup_signal_handler();

	port = atoi(argv[1]);
	proto_ver = atoi(argv[2]);
	clean_start = strcasecmp(argv[3], "false");
	if(argc == 5){
		command = argv[4];
	}

	mosquitto_lib_init();

	mosq = mosquitto_new("fuzzish", clean_start, NULL);
	if(mosq == NULL){
		return 1;
	}
	mosquitto_user_data_set(mosq, command);

	mosquitto_pre_connect_callback_set(mosq, on_pre_connect);

	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_connect_with_flags_callback_set(mosq, on_connect_with_flags);
	mosquitto_connect_v5_callback_set(mosq, on_connect_v5);

	mosquitto_disconnect_callback_set(mosq, on_disconnect);
	mosquitto_disconnect_v5_callback_set(mosq, on_disconnect_v5);

	mosquitto_publish_callback_set(mosq, on_publish);
	mosquitto_publish_v5_callback_set(mosq, on_publish_v5);

	mosquitto_message_callback_set(mosq, on_message);
	mosquitto_message_v5_callback_set(mosq, on_message_v5);

	mosquitto_subscribe_callback_set(mosq, on_subscribe);
	mosquitto_subscribe_v5_callback_set(mosq, on_subscribe_v5);

	mosquitto_unsubscribe_callback_set(mosq, on_unsubscribe);
	mosquitto_unsubscribe_v5_callback_set(mosq, on_unsubscribe_v5);
	mosquitto_unsubscribe2_v5_callback_set(mosq, on_unsubscribe2_v5);

	mosquitto_log_callback_set(mosq, on_log);

	mosquitto_int_option(mosq, MOSQ_OPT_PROTOCOL_VERSION, proto_ver);

	rc = mosquitto_connect(mosq, "localhost", port, 60);
	if(rc != MOSQ_ERR_SUCCESS){
		printf("bad connect\n");
		return rc;
	}

	while(run == -1){
		mosquitto_loop(mosq, -1, 1);
	}

	mosquitto_destroy(mosq);

	mosquitto_lib_cleanup();
	return 0;
}
