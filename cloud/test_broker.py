#!/usr/bin/env python3
"""
Quick test script for the custom MQTT broker
Publishes test messages to verify broker functionality
"""

import paho.mqtt.client as mqtt
import json
import time
import sys

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("✅ Connected to broker")
    else:
        print(f"❌ Connection failed with code {rc}")
        sys.exit(1)

def main():
    print("=" * 60)
    print("🧪 Testing Custom MQTT Broker")
    print("=" * 60)
    print()
    print("Make sure mqtt_broker.py is running!")
    print()
    
    # Create client
    client = mqtt.Client("test_publisher")
    client.on_connect = on_connect
    
    try:
        # Connect
        print("🔌 Connecting to localhost:1883...")
        client.connect("localhost", 1883, 60)
        client.loop_start()
        time.sleep(1)
        
        # Publish test messages
        test_messages = [
            ("reactor/sensors", {"temp": 25.5, "accel_x": 0.12, "accel_y": 0.05, "accel_z": 9.81}),
            ("reactor/telemetry", {"sample_id": 42, "status": "ok"}),
            ("reactor/alerts", {"level": "warning", "message": "Temperature rising"}),
        ]
        
        print("\n📨 Publishing test messages...\n")
        
        for topic, data in test_messages:
            json_data = json.dumps(data)
            result = client.publish(topic, json_data)
            
            if result.rc == mqtt.MQTT_ERR_SUCCESS:
                print(f"✅ Published to '{topic}':")
                print(f"   {json_data}")
            else:
                print(f"❌ Failed to publish to '{topic}'")
            
            time.sleep(0.5)
        
        print("\n✨ Test complete!")
        print("Check mqtt_test_client.py to see if messages were received")
        
        client.loop_stop()
        client.disconnect()
        
    except ConnectionRefusedError:
        print("\n❌ Could not connect to broker")
        print("   Make sure mqtt_broker.py is running!")
        sys.exit(1)
    except Exception as e:
        print(f"\n❌ Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()

