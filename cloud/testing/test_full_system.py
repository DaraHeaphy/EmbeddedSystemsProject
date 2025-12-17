#!/usr/bin/env python3

import paho.mqtt.client as mqtt
import json
import time
import sys
import os

BROKER_HOST = "alderaan.software-engineering.ie"
BROKER_PORT = 1883
TEST_TOPIC = "test/system_check"

messages_received = []
publisher_connected = False
subscriber_connected = False


def on_sub_connect(client, userdata, flags, rc):
    global subscriber_connected
    if rc == 0:
        print("subscriber connected")
        subscriber_connected = True
        client.subscribe(TEST_TOPIC)
    else:
        print(f"subscriber failed: {rc}")


def on_sub_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
        messages_received.append(payload)
        print(f"received: {payload}")
    except Exception as e:
        print(f"parse error: {e}")


def on_pub_connect(client, userdata, flags, rc):
    global publisher_connected
    if rc == 0:
        print("publisher connected")
        publisher_connected = True
    else:
        print(f"publisher failed: {rc}")


def test_system():
    print(f"full system test - {BROKER_HOST}:{BROKER_PORT}")
    print(f"topic: {TEST_TOPIC}")

    subscriber = mqtt.Client(client_id=f"test_sub_{os.getpid()}")
    subscriber.on_connect = on_sub_connect
    subscriber.on_message = on_sub_message

    publisher = mqtt.Client(client_id=f"test_pub_{os.getpid()}")
    publisher.on_connect = on_pub_connect

    try:
        subscriber.connect(BROKER_HOST, BROKER_PORT, 60)
        subscriber.loop_start()
        time.sleep(2)

        if not subscriber_connected:
            print("subscriber timeout")
            return False

        publisher.connect(BROKER_HOST, BROKER_PORT, 60)
        publisher.loop_start()
        time.sleep(2)

        if not publisher_connected:
            print("publisher timeout")
            return False

        test_data = [
            {"test": 1, "message": "hello"},
            {"test": 2, "temp": 25.5, "power": 65},
            {"test": 3, "state": "NORMAL"}
        ]

        print("publishing test messages...")
        for data in test_data:
            publisher.publish(TEST_TOPIC, json.dumps(data), qos=1)
            time.sleep(0.5)

        time.sleep(3)

        print(f"published: {len(test_data)}, received: {len(messages_received)}")

        if len(messages_received) == len(test_data):
            print("success - all messages received")
            return True
        elif len(messages_received) > 0:
            print("partial success - some messages received")
            return True
        else:
            print("failed - no messages received")
            return False

    except Exception as e:
        print(f"error: {e}")
        return False
    finally:
        publisher.loop_stop()
        subscriber.loop_stop()
        publisher.disconnect()
        subscriber.disconnect()


if __name__ == "__main__":
    success = test_system()
    sys.exit(0 if success else 1)
