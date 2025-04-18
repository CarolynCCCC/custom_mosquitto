#!/usr/bin/env python3

from mosq_test_helper import *

def do_test(testdir):
    port = mosq_test.get_port()

    resp_topic = "response/topic"
    pub_topic = "request/topic"

    rc = 1
    connect1_packet = mosq_test.gen_connect("request-test", proto_ver=5)
    connect2_packet = mosq_test.gen_connect("response-test", proto_ver=5)
    connack_packet = mosq_test.gen_connack(rc=0, proto_ver=5)

    mid = 1
    subscribe1_packet = mosq_test.gen_subscribe(mid, resp_topic, 0, proto_ver=5)
    subscribe2_packet = mosq_test.gen_subscribe(mid, pub_topic, 0, proto_ver=5)
    suback_packet = mosq_test.gen_suback(mid, 0, proto_ver=5)


    props = mqtt5_props.gen_string_prop(mqtt5_props.PROP_RESPONSE_TOPIC, resp_topic)
    props += mqtt5_props.gen_string_prop(mqtt5_props.PROP_CORRELATION_DATA, "corridor")
    publish1_packet_incoming = mosq_test.gen_publish(pub_topic, qos=0, payload="action", proto_ver=5, properties=props)

    props = mqtt5_props.gen_string_prop(mqtt5_props.PROP_RESPONSE_TOPIC, resp_topic)
    props += mqtt5_props.gen_string_prop(mqtt5_props.PROP_CORRELATION_DATA, "corridor")
    props += mqtt5_props.gen_string_pair_prop(mqtt5_props.PROP_USER_PROPERTY, "user", "data")
    publish1_packet_outgoing = mosq_test.gen_publish(pub_topic, qos=0, payload="action", proto_ver=5, properties=props)

    props = mqtt5_props.gen_string_prop(mqtt5_props.PROP_CORRELATION_DATA, "corridor")
    publish2_packet = mosq_test.gen_publish(resp_topic, qos=0, payload="a response", proto_ver=5, properties=props)
    publish2_packet_outgoing = mosq_test.gen_publish(pub_topic, qos=0, payload="action", proto_ver=5, properties=props)


    sock = mosq_test.listen_sock(port);

    env = dict(os.environ)
    client1 = mosq_test.start_client(filename=f"{testdir}-03-request-response-correlation-1.log", cmd=[f"{testdir}/03-request-response-correlation-1.test", str(port)])

    try:
        (conn1, address) = sock.accept()
        conn1.settimeout(10)

        client2 = mosq_test.start_client(filename=f"{testdir}-03-request-response-2.log", cmd=[f"{testdir}/03-request-response-2.test", str(port)])
        (conn2, address) = sock.accept()
        conn2.settimeout(10)

        mosq_test.do_receive_send(conn1, connect1_packet, connack_packet, "connect1")
        mosq_test.do_receive_send(conn2, connect2_packet, connack_packet, "connect2")

        mosq_test.do_receive_send(conn1, subscribe1_packet, suback_packet, "subscribe1")
        mosq_test.do_receive_send(conn2, subscribe2_packet, suback_packet, "subscribe2")

        mosq_test.expect_packet(conn1, "publish1", publish1_packet_incoming)
        conn2.send(publish1_packet_outgoing)

        mosq_test.expect_packet(conn2, "publish2", publish2_packet)
        conn1.send(publish2_packet_outgoing)
        rc = 0

        conn1.close()
        conn2.close()
    except mosq_test.TestError:
        pass
        if mosq_test.wait_for_subprocess(client1):
            print("client1 not terminated")
            if rc == 0: rc=1
        if mosq_test.wait_for_subprocess(client2):
            print("client2 not terminated")
            if rc == 0: rc=1
        if rc:
            (stdo, stde) = client1.communicate()
            print(stde)
            (stdo, stde) = client2.communicate()
            print(stde)
            exit(1)

do_test("c")
do_test("cpp")
