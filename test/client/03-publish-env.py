#!/usr/bin/env python3

#

from mosq_test_helper import *

def do_test(proto_ver, env):
    rc = 1

    port = mosq_test.get_port()

    if proto_ver == 5:
        V = 'mqttv5'
    elif proto_ver == 4:
        V = 'mqttv311'
    else:
        V = 'mqttv31'

    env = mosq_test.env_add_ld_library_path(env)
    cmd = [mosq_test.get_build_root() + '/client/mosquitto_pub',
            '-p', str(port),
            '-q', '1',
            '-V', V
            ]

    mid = 1
    publish_packet = mosq_test.gen_publish("env/config/file/pub", qos=1, mid=mid, payload="message", proto_ver=proto_ver)
    if proto_ver == 5:
        puback_packet = mosq_test.gen_puback(mid, proto_ver=proto_ver, reason_code=mqtt5_rc.MQTT_RC_NO_MATCHING_SUBSCRIBERS)
    else:
        puback_packet = mosq_test.gen_puback(mid, proto_ver=proto_ver)

    broker = mosq_test.start_broker(filename=os.path.basename(__file__), port=port)

    try:
        sock = mosq_test.sub_helper(port=port, topic="#", qos=1, proto_ver=proto_ver)

        pub = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        pub_terminate_rc = 0
        if mosq_test.wait_for_subprocess(pub):
            print("pub not terminated")
            pub_terminate_rc = 1
        (stdo, stde) = pub.communicate()

        mosq_test.expect_packet(sock, "publish", publish_packet)
        rc = pub_terminate_rc
        sock.close()
    except mosq_test.TestError:
        pass
    except Exception as e:
        print(e)
    finally:
        broker.terminate()
        if mosq_test.wait_for_subprocess(broker):
            print("broker not terminated")
            if rc == 0: rc=1
        (stdo, stde) = broker.communicate()
        if rc:
            print(stde.decode('utf-8'))
            print("proto_ver=%d" % (proto_ver))
            exit(rc)


env = {'HOME': str(source_dir / 'data')}
do_test(proto_ver=3, env=env)
do_test(proto_ver=4, env=env)
do_test(proto_ver=5, env=env)

env = {'XDG_CONFIG_HOME': str(source_dir / 'data/.config')}
do_test(proto_ver=3, env=env)
do_test(proto_ver=4, env=env)
do_test(proto_ver=5, env=env)
