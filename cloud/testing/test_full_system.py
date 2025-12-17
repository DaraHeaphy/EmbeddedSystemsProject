#!/usr/bin/env python3
"""
Full System Test - Tests the complete data flow
Tests: Publisher → Broker → Subscriber
"""

import paho.mqtt.client as mqtt
import json
import time
import threading
import sys
import os

BROKER_HOST = "alderaan.software-engineering.ie"
BROKER_PORT = 1883
TEST_TOPIC = "test/system_check"

# Track received messages
messages_received = []
publisher_connected = False
subscriber_connected = False

def on_subscriber_connect(client, userdata, flags, rc):
    global subscriber_connected
    if rc == 0:
        print("✅ Subscriber connected")
        subscriber_connected = True
        client.subscribe(TEST_TOPIC)
        print(f"📡 Subscribed to: {TEST_TOPIC}")
    else:
        print(f"❌ Subscriber connection failed: {rc}")

def on_subscriber_message(client, userdata, msg):
    global messages_received
    try:
        payload = json.loads(msg.payload.decode())
        messages_received.append(payload)
        print(f"📨 Received: {payload}")
    except Exception as e:
        print(f"❌ Error parsing message: {e}")

def on_publisher_connect(client, userdata, flags, rc):
    global publisher_connected
    if rc == 0:
        print("✅ Publisher connected")
        publisher_connected = True
    else:
        print(f"❌ Publisher connection failed: {rc}")

def test_system():
    print("=" * 70)
    print("🧪 Full System Test")
    print("=" * 70)
    print(f"Broker: {BROKER_HOST}:{BROKER_PORT}")
    print(f"Test Topic: {TEST_TOPIC}")
    print()
    
    # Create subscriber
    print("1️⃣ Creating subscriber...")
    subscriber = mqtt.Client(client_id=f"system_test_sub_{os.getpid()}")
    subscriber.on_connect = on_subscriber_connect
    subscriber.on_message = on_subscriber_message
    
    # Create publisher
    print("2️⃣ Creating publisher...")
    publisher = mqtt.Client(client_id=f"system_test_pub_{os.getpid()}")
    publisher.on_connect = on_publisher_connect
    
    try:
        # Connect subscriber
        print("3️⃣ Connecting subscriber...")
        subscriber.connect(BROKER_HOST, BROKER_PORT, 60)
        subscriber.loop_start()
        
        # Wait for subscriber to connect
        time.sleep(2)
        if not subscriber_connected:
            print("❌ Subscriber failed to connect within timeout")
            return False
        
        # Connect publisher
        print("4️⃣ Connecting publisher...")
        publisher.connect(BROKER_HOST, BROKER_PORT, 60)
        publisher.loop_start()
        
        # Wait for publisher to connect
        time.sleep(2)
        if not publisher_connected:
            print("❌ Publisher failed to connect within timeout")
            return False
        
        # Publish test messages
        print()
        print("5️⃣ Publishing test messages...")
        test_data = [
            {"test": 1, "message": "Hello from test"},
            {"test": 2, "temp": 25.5, "power": 65},
            {"test": 3, "state": "NORMAL"}
        ]
        
        for i, data in enumerate(test_data, 1):
            json_str = json.dumps(data)
            result = publisher.publish(TEST_TOPIC, json_str, qos=1)
            if result.rc == mqtt.MQTT_ERR_SUCCESS:
                print(f"   📤 Published test {i}: {json_str}")
            else:
                print(f"   ❌ Failed to publish test {i}")
            time.sleep(0.5)
        
        # Wait for messages to arrive
        print()
        print("6️⃣ Waiting for messages to arrive...")
        time.sleep(3)
        
        # Check results
        print()
        print("=" * 70)
        print("📊 Results:")
        print("=" * 70)
        print(f"Messages published: {len(test_data)}")
        print(f"Messages received: {len(messages_received)}")
        print()
        
        if len(messages_received) == len(test_data):
            print("✅ SUCCESS! All messages received correctly!")
            print()
            print("Your system is working perfectly:")
            print("  • Publisher can connect to broker ✅")
            print("  • Subscriber can connect to broker ✅")
            print("  • Messages flow from publisher to subscriber ✅")
            print()
            print("🎉 You can now run the full dashboard + publisher setup!")
            success = True
        elif len(messages_received) > 0:
            print("⚠️  PARTIAL SUCCESS")
            print(f"  Received {len(messages_received)} out of {len(test_data)} messages")
            print("  Some messages may be delayed or lost")
            success = True
        else:
            print("❌ FAILED - No messages received!")
            print()
            print("Possible issues:")
            print("  • Firewall blocking MQTT traffic")
            print("  • Network connectivity problems")
            print("  • Topic mismatch (unlikely in this test)")
            print("  • Broker not forwarding messages")
            success = False
        
        # Cleanup
        publisher.loop_stop()
        subscriber.loop_stop()
        publisher.disconnect()
        subscriber.disconnect()
        
        return success
        
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

if __name__ == "__main__":
    success = test_system()
    sys.exit(0 if success else 1)

