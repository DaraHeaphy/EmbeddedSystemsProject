#!/usr/bin/env python3
"""
MQTT Publisher - Simulates your ESP32/C code publishing data
Use this to test that your Python subscriber is working correctly
"""

import paho.mqtt.client as mqtt
import json
import time
import sys
from datetime import datetime
import random

# Broker configuration
BROKER_HOST = "alderaan.software-engineering.ie"
BROKER_PORT = 1883

# Set these if authentication is required
USERNAME = None  # Set to your username if needed
PASSWORD = None  # Set to your password if needed

# Publishing configuration
PUBLISH_TOPIC = "reactor/sensors"  # CUSTOMIZE THIS!
PUBLISH_INTERVAL = 2  # seconds between messages

def on_connect(client, userdata, flags, rc):
    """Callback when connected"""
    if rc == 0:
        print("✅ Connected to broker")
        userdata['connected'] = True
    else:
        print(f"❌ Connection failed with code {rc}")
        sys.exit(1)

def on_publish(client, userdata, mid):
    """Callback when message is published"""
    userdata['message_count'] += 1

def generate_sensor_data(sample_id):
    """Generate fake sensor data (simulating ESP32)"""
    import math
    
    temp = round(20.0 + random.uniform(-5, 15), 2)
    accel_x = round(random.uniform(-0.5, 0.5), 3)
    accel_y = round(random.uniform(-0.5, 0.5), 3)
    accel_z = round(9.81 + random.uniform(-0.2, 0.2), 3)
    
    # Calculate acceleration magnitude
    accel_mag = round(math.sqrt(accel_x**2 + accel_y**2 + accel_z**2), 3)
    
    # Generate power
    power = int(50 + random.uniform(-20, 30))
    power = max(0, min(100, power))  # Clamp between 0-100
    
    # Determine state based on temperature
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
    print("=" * 70)
    print("🧪 MQTT Publisher Test (Simulating ESP32/C code)")
    print("=" * 70)
    print()
    print(f"Broker: {BROKER_HOST}:{BROKER_PORT}")
    print(f"Topic: {PUBLISH_TOPIC}")
    print(f"Interval: {PUBLISH_INTERVAL}s")
    print()
    
    # Create client
    userdata = {'connected': False, 'message_count': 0}
    client = mqtt.Client("test_publisher")
    client.user_data_set(userdata)
    
    if USERNAME and PASSWORD:
        client.username_pw_set(USERNAME, PASSWORD)
        print(f"🔐 Using authentication: {USERNAME}")
    
    client.on_connect = on_connect
    client.on_publish = on_publish
    
    try:
        # Connect
        print("🔌 Connecting...")
        client.connect(BROKER_HOST, BROKER_PORT, 60)
        client.loop_start()
        
        # Wait for connection
        timeout = 10
        start_time = time.time()
        while not userdata['connected']:
            if time.time() - start_time > timeout:
                print("❌ Connection timeout")
                sys.exit(1)
            time.sleep(0.1)
        
        print()
        print("✅ Connected! Publishing messages...")
        print("   (Make sure test_subscriber.py is running in another terminal)")
        print()
        print("Press Ctrl+C to stop")
        print("=" * 70)
        print()
        
        # Publish messages continuously
        sample_id = 0
        while True:
            # Generate sensor data
            data = generate_sensor_data(sample_id)
            json_data = json.dumps(data)
            sample_id += 1
            
            # Publish
            result = client.publish(PUBLISH_TOPIC, json_data, qos=1)
            
            if result.rc == mqtt.MQTT_ERR_SUCCESS:
                timestamp = datetime.now().strftime('%H:%M:%S')
                print(f"📤 [{timestamp}] Published #{userdata['message_count']+1}")
                print(f"   Topic: {PUBLISH_TOPIC}")
                print(f"   Data: {json_data}")
                print()
            else:
                print(f"❌ Failed to publish")
            
            # Wait before next message
            time.sleep(PUBLISH_INTERVAL)
            
    except KeyboardInterrupt:
        print()
        print("=" * 70)
        print("👋 Shutting down publisher...")
        print(f"   Total messages published: {userdata['message_count']}")
        print("=" * 70)
        client.loop_stop()
        client.disconnect()
        
    except ConnectionRefusedError:
        print()
        print("❌ Connection refused!")
        print("   Run test_connectivity.py first")
        sys.exit(1)
        
    except Exception as e:
        print(f"❌ Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()

