#!/usr/bin/env python3

import paho.mqtt.client as mqtt
import time
import sys
import os

BROKER_HOST = "alderaan.software-engineering.ie"
BROKER_PORT = 1883


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("connected to broker")
        print(f"  host: {BROKER_HOST}")
        print(f"  port: {BROKER_PORT}")
        userdata['connected'] = True
    else:
        errors = {
            1: "incorrect protocol version",
            2: "invalid client identifier",
            3: "server unavailable",
            4: "bad username or password",
            5: "not authorized"
        }
        print(f"connection failed: {errors.get(rc, 'unknown error')} (code {rc})")
        userdata['connected'] = False
        userdata['error_code'] = rc


def test_anonymous():
    print(f"testing connection to {BROKER_HOST}:{BROKER_PORT}")
    userdata = {'connected': False, 'error_code': None}

    client = mqtt.Client(client_id=f"connectivity_test_{os.getpid()}")
    client.user_data_set(userdata)
    client.on_connect = on_connect

    try:
        client.connect(BROKER_HOST, BROKER_PORT, 60)
        client.loop_start()

        start = time.time()
        while not userdata['connected'] and userdata['error_code'] is None:
            if time.time() - start > 10:
                print("connection timeout")
                return False
            time.sleep(0.1)

        if userdata['connected']:
            print("success - broker is reachable")
            client.loop_stop()
            client.disconnect()
            return True
        elif userdata['error_code'] in (4, 5):
            print("authentication required - ask for credentials")
        return False

    except Exception as e:
        print(f"error: {e}")
        return False
    finally:
        try:
            client.loop_stop()
            client.disconnect()
        except:
            pass


def test_with_credentials(username, password):
    print(f"testing with credentials: {username}")
    userdata = {'connected': False, 'error_code': None}

    client = mqtt.Client(client_id=f"connectivity_test_auth_{os.getpid()}")
    client.user_data_set(userdata)
    client.username_pw_set(username, password)
    client.on_connect = on_connect

    try:
        client.connect(BROKER_HOST, BROKER_PORT, 60)
        client.loop_start()

        start = time.time()
        while not userdata['connected'] and userdata['error_code'] is None:
            if time.time() - start > 10:
                print("connection timeout")
                return False
            time.sleep(0.1)

        if userdata['connected']:
            print("authentication successful")
            client.loop_stop()
            client.disconnect()
            return True
        print("authentication failed")
        return False

    except Exception as e:
        print(f"error: {e}")
        return False
    finally:
        try:
            client.loop_stop()
            client.disconnect()
        except:
            pass


if __name__ == "__main__":
    if len(sys.argv) > 2:
        success = test_with_credentials(sys.argv[1], sys.argv[2])
    else:
        success = test_anonymous()
    sys.exit(0 if success else 1)
