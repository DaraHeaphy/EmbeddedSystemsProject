#!/usr/bin/env python3
"""
MQTT Test Client - Subscribes to reactor telemetry topics
Used for testing the ESP32 MQTT publisher locally
"""

import paho.mqtt.client as mqtt
import json
import sys
from datetime import datetime

# Configuration
BROKER_HOST = "localhost"  # Change to your broker IP if needed
BROKER_PORT = 1883
CLIENT_ID = "reactor_test_client"

# Topics to subscribe to
TOPICS = [
    "reactor/sensors",
    "reactor/telemetry",
    "reactor/alerts",
    "reactor/#"  # Wildcard to catch all reactor topics
]

def on_connect(client, userdata, flags, rc):
    """Callback when client connects to broker"""
    if rc == 0:
        print(f" Connected to MQTT broker at {BROKER_HOST}:{BROKER_PORT}")
        print("📡 Subscribing to topics:")
        for topic in TOPICS:
            client.subscribe(topic)
            print(f"   - {topic}")
        print("\n Waiting for messages... (Press Ctrl+C to exit)\n")
    else:
        print(f" Connection failed with code {rc}")
        sys.exit(1)

def on_message(client, userdata, msg):
    """Callback when a message is received"""
    timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    
    print(f"[{timestamp}] 📩 Topic: {msg.topic}")
    
    try:
        # Try to parse as JSON for pretty printing
        payload = msg.payload.decode('utf-8')
        data = json.loads(payload)
        print(f"           Data: {json.dumps(data, indent=18)}")
    except json.JSONDecodeError:
        # If not JSON, print raw
        print(f"           Data: {msg.payload.decode('utf-8')}")
    except Exception as e:
        print(f"           Raw: {msg.payload}")
    
    print()

def on_disconnect(client, userdata, rc):
    """Callback when client disconnects"""
    if rc != 0:
        print(f"  Unexpected disconnection (code {rc})")

def main():
    """Main function"""
    print("=" * 60)
    print(" MQTT Test Client for ESP32 Reactor Publisher")
    print("=" * 60)
    print()
    
    # Create MQTT client
    client = mqtt.Client(CLIENT_ID)
    
    # Set callbacks
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    
    try:
        # Connect to broker
        print(f"🔌 Connecting to broker at {BROKER_HOST}:{BROKER_PORT}...")
        client.connect(BROKER_HOST, BROKER_PORT, 60)
        
        # Start listening (blocking call)
        client.loop_forever()
        
    except KeyboardInterrupt:
        print("\n\n👋 Shutting down gracefully...")
        client.disconnect()
        sys.exit(0)
    except ConnectionRefusedError:
        print(f"\n Could not connect to MQTT broker at {BROKER_HOST}:{BROKER_PORT}")
        print("   Make sure Mosquitto is running!")
        print("   Run: mosquitto -v")
        sys.exit(1)
    except Exception as e:
        print(f"\n Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()

