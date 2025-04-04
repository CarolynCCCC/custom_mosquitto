#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mosquitto.h>
#include <mosquitto/mqtt_protocol.h>

static int run = -1;

static void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
	(void)obj;

	if(rc){
		exit(1);
	}else{
		mosquitto_subscribe(mosq, NULL, "request/topic", 0);
	}
}

static void on_message_v5(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg, const mosquitto_property *props)
{
	const mosquitto_property *p_resp, *p_corr = NULL;
	char *resp_topic = NULL;
	int rc;

	(void)obj;

	if(!strcmp(msg->topic, "request/topic")){
		p_resp = mosquitto_property_read_string(props, MQTT_PROP_RESPONSE_TOPIC, &resp_topic, false);
		if(p_resp){
			p_corr = mosquitto_property_read_binary(props, MQTT_PROP_CORRELATION_DATA, NULL, NULL, false);
			rc = mosquitto_publish_v5(mosq, NULL, resp_topic, strlen("a response"), "a response", 0, false, p_corr);
			if(rc != MOSQ_ERR_SUCCESS){
				abort();
			}
			free(resp_topic);
		}
	}
}

static void on_publish(struct mosquitto *mosq, void *obj, int mid)
{
	(void)mosq;
	(void)obj;
	(void)mid;

	run = 0;
}


int main(int argc, char *argv[])
{
	int rc;
	struct mosquitto *mosq;
	int ver = PROTOCOL_VERSION_v5;
	int port;

	if(argc < 2){
		return 1;
	}
	port = atoi(argv[1]);

	mosquitto_lib_init();

	mosq = mosquitto_new("response-test", true, NULL);
	if(mosq == NULL){
		return 1;
	}
	mosquitto_opts_set(mosq, MOSQ_OPT_PROTOCOL_VERSION, &ver);
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_publish_callback_set(mosq, on_publish);
	mosquitto_message_v5_callback_set(mosq, on_message_v5);

	rc = mosquitto_connect(mosq, "localhost", port, 60);
	if(rc != MOSQ_ERR_SUCCESS) return rc;

	while(run == -1){
		rc = mosquitto_loop(mosq, -1, 1);
	}
	mosquitto_destroy(mosq);

	mosquitto_lib_cleanup();
	return run;
}
