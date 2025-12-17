#!/usr/bin/env python3

import paho.mqtt.client as mqtt
import json
import time
import sys
import math
import random
import os
from datetime import datetime

BROKER_HOST = "alderaan.software-engineering.ie"
BROKER_PORT = 1883
TOPIC = "reactor/sensors"
INTERVAL = 2


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("connected")
        userdata['connected'] = True
    else:
        print(f"connection failed: {rc}")
        sys.exit(1)


def on_publish(client, userdata, mid):
    userdata['count'] += 1


def generate_data(sample_id):
    temp = round(20.0 + random.uniform(-5, 15), 2)
    accel_x = round(random.uniform(-0.5, 0.5), 3)
    accel_y = round(random.uniform(-0.5, 0.5), 3)
    accel_z = round(9.81 + random.uniform(-0.2, 0.2), 3)
    accel_mag = round(math.sqrt(accel_x**2 + accel_y**2 + accel_z**2), 3)
    power = max(0, min(100, int(50 + random.uniform(-20, 30))))

    if temp > 80:
        state = "SCRAM"
    elif temp > 60:
        state = "WARNING"
    else:
        state = "NORMAL"

    return {
        "temp": temp,
        "accel_x": accel_x,
        "accel_y": accel_y,
        "accel_z": accel_z,
        "accel_mag": accel_mag,
        "power": power,
        "state": state,
        "sample_id": sample_id,
        "timestamp": datetime.now().isoformat(),
        "device_id": "test_publisher"
    }


def main():
    print(f"publisher - {BROKER_HOST}:{BROKER_PORT}")
    print(f"topic: {TOPIC}, interval: {INTERVAL}s")

    userdata = {'connected': False, 'count': 0}
    client = mqtt.Client(client_id=f"test_publisher_{os.getpid()}")
    client.user_data_set(userdata)
    client.on_connect = on_connect
    client.on_publish = on_publish

    try:
        client.connect(BROKER_HOST, BROKER_PORT, 60)
        client.loop_start()

        start = time.time()
        while not userdata['connected']:
            if time.time() - start > 10:
                print("connection timeout")
                sys.exit(1)
            time.sleep(0.1)

        print("publishing... (ctrl+c to stop)")

        sample_id = 0
        while True:
            data = generate_data(sample_id)
            client.publish(TOPIC, json.dumps(data), qos=1)
            print(f"[{datetime.now().strftime('%H:%M:%S')}] #{sample_id} temp={data['temp']} state={data['state']}")
            sample_id += 1
            time.sleep(INTERVAL)

    except KeyboardInterrupt:
        print(f"\nstopped - published {userdata['count']} messages")
        client.loop_stop()
        client.disconnect()

    except Exception as e:
        print(f"error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
