#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mosquitto.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

/* Rest of the includes and code */

static int run = -1;

static void do_loop(struct mosquitto *mosq)
{
	int sock;
	struct timeval tv;
	fd_set readfds, writefds;
	int fdcount;

	sock = mosquitto_socket(mosq);

	if(sock < 0) exit(1);

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);

	FD_SET(sock, &readfds);

	while(run == -1){
		tv.tv_sec = 0;
		tv.tv_usec = 100000;

		FD_SET(sock, &readfds);
		if(mosquitto_want_write(mosq)){
			FD_SET(sock, &writefds);
		}else{
			FD_CLR(sock, &writefds);
		}

		fdcount = select(sock+1, &readfds, &writefds, NULL, &tv);
		if(fdcount < 0) exit(1);
		if(FD_ISSET(sock, &readfds)){
			mosquitto_loop_read(mosq, 1);
		}
		if(FD_ISSET(sock, &writefds)){
			mosquitto_loop_write(mosq, 1);
		}
		mosquitto_loop_misc(mosq);
	}
}


static void on_connect_v5(struct mosquitto *mosq, void *obj, int rc, int flags, const mosquitto_property *properties)
{
	(void)mosq;
	(void)obj;
	(void)flags;
	(void)properties;

	if(rc){
		exit(1);
	}else{
		mosquitto_subscribe_v5(mosq, NULL, "loop/test", 0, 0, NULL);
	}
}

static void on_disconnect_v5(struct mosquitto *mosq, void *obj, int rc, const mosquitto_property *properties)
{
	(void)mosq;
	(void)obj;
	(void)properties;

	run = rc;
}

static void on_subscribe_v5(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos, const mosquitto_property *props)
{
	(void)obj;
	(void)mid;
	(void)qos_count;
	(void)granted_qos;
	(void)props;

	mosquitto_publish_v5(mosq, NULL, "loop/test", strlen("message"), "message", 0, false, NULL);
}

static void on_message_v5(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg, const mosquitto_property *properties)
{
	(void)mosq;
	(void)obj;
	(void)msg;
	(void)properties;

	mosquitto_disconnect(mosq);
}

int main(int argc, char *argv[])
{
	int rc;
	struct mosquitto *mosq;
	int port;

	if(argc < 2){
		return 1;
	}
	port = atoi(argv[1]);

	mosquitto_lib_init();

	mosq = mosquitto_new("loop-test", true, NULL);
	if(mosq == NULL){
		return 1;
	}
	mosquitto_int_option(mosq, MOSQ_OPT_PROTOCOL_VERSION, MQTT_PROTOCOL_V5);

	mosquitto_connect_v5_callback_set(mosq, on_connect_v5);
	mosquitto_disconnect_v5_callback_set(mosq, on_disconnect_v5);
	mosquitto_subscribe_v5_callback_set(mosq, on_subscribe_v5);
	mosquitto_message_v5_callback_set(mosq, on_message_v5);

	rc = mosquitto_connect_bind_v5(mosq, "localhost", port, 60, NULL, NULL);
	if(rc != MOSQ_ERR_SUCCESS) return rc;

	do_loop(mosq);
	mosquitto_destroy(mosq);

	mosquitto_lib_cleanup();
	return 1;
}
