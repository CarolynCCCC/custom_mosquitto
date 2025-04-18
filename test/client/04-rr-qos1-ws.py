#!/usr/bin/env python3

#

from mosq_test_helper import *

def write_config(filename, port1, port2):
    with open(filename, 'w') as f:
        f.write("allow_anonymous true\n")
        f.write(f"listener {port1}\n")
        f.write("protocol websockets\n")
        f.write(f"listener {port2}\n")

def do_test(proto_ver):
    rc = 1

    ports = mosq_test.get_port(2)
    conf_file = os.path.basename(__file__).replace('.py', '.conf')

    if proto_ver == 5:
        V = 'mqttv5'
    elif proto_ver == 4:
        V = 'mqttv311'
    else:
        V = 'mqttv31'

    env = {
        'XDG_CONFIG_HOME':'/tmp/missing'
    }
    env = mosq_test.env_add_ld_library_path(env)
    payload = "message"
    cmd = [f'{mosq_test.get_build_root()}/client/mosquitto_rr',
            '-p', str(ports[0]),
            '-q', '1',
            '-t', '04/rr/qos1/test/request',
            '-e', '04/rr/qos1/test/response',
            '-V', V,
            '-m', payload,
            '--ws'
            ]

    if proto_ver == 5:
        props = mqtt5_props.gen_string_prop(mqtt5_props.PROP_RESPONSE_TOPIC, "04/rr/qos1/test/response")
    else:
        props = None
    publish_packet_req = mosq_test.gen_publish("04/rr/qos1/test/request", qos=1, mid=1, payload=payload, proto_ver=proto_ver, properties=props)
    payload = "the response"
    publish_packet_resp = mosq_test.gen_publish("04/rr/qos1/test/response", qos=1, mid=2, payload=payload, proto_ver=proto_ver)
    puback_packet_req = mosq_test.gen_puback(1, proto_ver=proto_ver)
    puback_packet_resp = mosq_test.gen_puback(2, proto_ver=proto_ver)

    write_config(conf_file, ports[0], ports[1])

    try:
        broker = mosq_test.start_broker(filename=os.path.basename(__file__), port=ports[1], use_conf=True)
        sock = mosq_test.sub_helper(port=ports[1], topic="04/rr/qos1/test/request", qos=1, proto_ver=proto_ver)

        rr = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)

        mosq_test.expect_packet(sock, "publish", publish_packet_req)
        sock.send(puback_packet_req)

        sock.send(publish_packet_resp)
        mosq_test.expect_packet(sock, "puback", puback_packet_resp)

        time.sleep(0.1)
        rr_terminate_rc = 0
        if mosq_test.wait_for_subprocess(rr):
            print("rr not terminated")
            rr_terminate_rc = 1
        (stdo, stde) = rr.communicate()
        if stdo.decode('utf-8') == payload + '\n':
            rc = rr_terminate_rc
        sock.close()
    except mosq_test.TestError:
        pass
    except Exception as e:
        print(e)
    finally:
        broker.terminate()
        os.remove(conf_file)
        if mosq_test.wait_for_subprocess(broker):
            print("broker not terminated")
            if rc == 0: rc=1
        (stdo, stde) = broker.communicate()
        if rc:
            print(stde.decode('utf-8'))
            print("proto_ver=%d" % (proto_ver))
            exit(rc)


do_test(proto_ver=3)
do_test(proto_ver=4)
do_test(proto_ver=5)
