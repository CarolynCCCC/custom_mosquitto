#!/usr/bin/env python3

from mosq_test_helper import *

if sys.version < '2.7':
    print("WARNING: SSL not supported on Python 2.6")
    exit(0)

def write_config1(filename, port1, port2):
    with open(filename, 'w') as f:
        f.write("allow_anonymous true\n")
        f.write("\n")
        f.write(f"psk_file {str(source_dir/'08-tls-psk-bridge.psk')}\n")
        f.write("\n")
        f.write("listener %d\n" % (port1))
        f.write("\n")
        f.write("listener %d\n" % (port2))
        f.write("psk_hint hint\n")

def write_config2(filename, port2, port3):
    with open(filename, 'w') as f:
        f.write("listener %d\n" % (port3))
        f.write("allow_anonymous true\n")
        f.write("\n")
        f.write("connection bridge-psk\n")
        f.write("address localhost:%d\n" % (port2))
        f.write("topic psk/test out\n")
        f.write("bridge_identity psk-test\n")
        f.write("bridge_psk deadbeef\n")

(port1, port2, port3) = mosq_test.get_port(3)
conf_file1 = "08-tls-psk-bridge.conf"
conf_file2 = "08-tls-psk-bridge.conf2"
write_config1(conf_file1, port1, port2)
write_config2(conf_file2, port2, port3)

env = mosq_test.env_add_ld_library_path()

rc = 1
connect_packet = mosq_test.gen_connect("no-psk-test-client")
connack_packet = mosq_test.gen_connack(rc=0)

mid = 1
subscribe_packet = mosq_test.gen_subscribe(mid, "psk/test", 0)
suback_packet = mosq_test.gen_suback(mid, 0)

publish_packet = mosq_test.gen_publish(topic="psk/test", payload="message", qos=0)

bridge_cmd = [mosq_test.get_build_root() + '/src/mosquitto', '-c', '08-tls-psk-bridge.conf2']
broker = mosq_test.start_broker(filename=os.path.basename(__file__), use_conf=True, port=port1)
bridge = mosq_test.start_broker(filename=os.path.basename(__file__)+'_bridge', cmd=bridge_cmd, port=port3)

pub = None
try:
    sock = mosq_test.do_client_connect(connect_packet, connack_packet, timeout=30, port=port1)

    mosq_test.do_send_receive(sock, subscribe_packet, suback_packet, "suback")

    pub = subprocess.run(['./c/08-tls-psk-bridge.test', str(port3)], env=env, capture_output=True, encoding='utf-8')
    if pub.returncode != 0:
        print("d")
        print(pub.returncode)
        raise ValueError

    mosq_test.expect_packet(sock, "publish", publish_packet)
    rc = pub.returncode

    sock.close()
except mosq_test.TestError:
    pass
finally:
    os.remove(conf_file1)
    os.remove(conf_file2)
    broker.terminate()
    if mosq_test.wait_for_subprocess(broker):
        print("broker not terminated")
        if rc == 0: rc=1
    bridge.terminate()
    if mosq_test.wait_for_subprocess(bridge):
        print("bridge not terminated")
        if rc == 0: rc=1
    (stdo, stde) = broker.communicate()
    if rc:
        print(stde.decode('utf-8'))
        (stdo, stde) = bridge.communicate()
        print(stde.decode('utf-8'))
        if pub:
            print(pub.stdout)
            print(pub.stderr)

exit(rc)

