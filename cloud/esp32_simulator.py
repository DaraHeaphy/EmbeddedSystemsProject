#!/usr/bin/env python3
"""
ESP32 Reactor MQTT Publisher Simulator
Simulates the ESP32 publishing telemetry data without hardware
"""

import paho.mqtt.client as mqtt
import json
import time
import random
import sys
from datetime import datetime

# Configuration (matches ESP32 settings)
BROKER_HOST = "localhost"
BROKER_PORT = 1883
CLIENT_ID = "reactor_core_001"  # Same as ESP32
MQTT_TOPIC = "reactor/telemetry"

# Simulation parameters
PUBLISH_INTERVAL = 1.0  # seconds (ESP32 publishes every 1s)
TEMP_BASE = 25.0
TEMP_VARIANCE = 5.0
ACCEL_BASE = 9.81
ACCEL_VARIANCE = 0.2

class ESP32Simulator:
    """Simulates ESP32 reactor telemetry publishing"""
    
    def __init__(self):
        self.sample_id = 0
        self.state = "NORMAL"
        self.power = 75
        self.temp = TEMP_BASE
        self.accel_mag = ACCEL_BASE
        self.connected = False
        
        # Create MQTT client
        self.client = mqtt.Client(CLIENT_ID)
        self.client.on_connect = self.on_connect
        self.client.on_disconnect = self.on_disconnect
        self.client.on_publish = self.on_publish
        
    def on_connect(self, client, userdata, flags, rc):
        """Callback when connected to broker"""
        if rc == 0:
            self.connected = True
            print("✅ ESP32 Simulator connected to MQTT broker")
            print(f"📡 Publishing to topic: {MQTT_TOPIC}")
            print(f"⏱️  Interval: {PUBLISH_INTERVAL}s")
            print("\n🎮 Controls:")
            print("   Press Ctrl+C to stop\n")
        else:
            print(f"❌ Connection failed with code {rc}")
            sys.exit(1)
    
    def on_disconnect(self, client, userdata, rc):
        """Callback when disconnected"""
        self.connected = False
        if rc != 0:
            print(f"⚠️  Unexpected disconnection")
    
    def on_publish(self, client, userdata, mid):
        """Callback when message is published"""
        # Silent to avoid spam, but you can enable for debugging
        pass
    
    def simulate_telemetry(self):
        """Generate simulated telemetry data"""
        # Temperature slowly drifts
        self.temp += random.uniform(-0.5, 0.5)
        self.temp = max(20.0, min(85.0, self.temp))
        
        # Accelerometer has small variations
        self.accel_mag = ACCEL_BASE + random.uniform(-ACCEL_VARIANCE, ACCEL_VARIANCE)
        
        # State transitions based on temperature
        if self.temp > 70.0 and self.state == "NORMAL":
            self.state = "WARNING"
            print(f"⚠️  [{self.sample_id}] State changed to WARNING (temp={self.temp:.2f}°C)")
        elif self.temp > 80.0 and self.state == "WARNING":
            self.state = "SCRAM"
            self.power = 0
            print(f"🚨 [{self.sample_id}] EMERGENCY SCRAM! (temp={self.temp:.2f}°C)")
        elif self.temp < 60.0 and self.state == "WARNING":
            self.state = "NORMAL"
            print(f"✅ [{self.sample_id}] State returned to NORMAL")
        
        # Power adjusts based on state
        if self.state == "NORMAL":
            self.power = min(100, self.power + random.randint(-2, 2))
        elif self.state == "WARNING":
            self.power = max(50, self.power - random.randint(0, 5))
        elif self.state == "SCRAM":
            self.power = 0
        
        # Build telemetry JSON (same format as ESP32)
        telemetry = {
            "sample_id": self.sample_id,
            "temp": round(self.temp, 2),
            "accel_mag": round(self.accel_mag, 3),
            "state": self.state,
            "power": self.power
        }
        
        return telemetry
    
    def publish_telemetry(self, telemetry):
        """Publish telemetry to MQTT"""
        json_data = json.dumps(telemetry)
        result = self.client.publish(MQTT_TOPIC, json_data, qos=1)
        
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            # Show summary every 10 samples
            if self.sample_id % 10 == 0:
                print(f"📊 [{self.sample_id:4d}] Temp={telemetry['temp']:.1f}°C  "
                      f"Accel={telemetry['accel_mag']:.2f}  "
                      f"State={telemetry['state']:8s}  "
                      f"Power={telemetry['power']:3d}%")
            return True
        else:
            print(f"❌ Failed to publish sample {self.sample_id}")
            return False
    
    def run(self):
        """Main simulation loop"""
        print("=" * 60)
        print("🔧 ESP32 Reactor MQTT Publisher Simulator")
        print("=" * 60)
        print()
        
        try:
            # Connect to broker
            print(f"🔌 Connecting to MQTT broker at {BROKER_HOST}:{BROKER_PORT}...")
            self.client.connect(BROKER_HOST, BROKER_PORT, 60)
            self.client.loop_start()
            
            # Wait for connection
            time.sleep(1)
            
            if not self.connected:
                print("❌ Failed to connect to broker")
                print("   Make sure mqtt_broker.py is running!")
                return
            
            # Simulation loop
            while True:
                # Generate and publish telemetry
                telemetry = self.simulate_telemetry()
                self.publish_telemetry(telemetry)
                
                self.sample_id += 1
                time.sleep(PUBLISH_INTERVAL)
                
        except KeyboardInterrupt:
            print("\n\n👋 Stopping simulator...")
        except ConnectionRefusedError:
            print(f"\n❌ Could not connect to MQTT broker at {BROKER_HOST}:{BROKER_PORT}")
            print("   Make sure mqtt_broker.py is running!")
        except Exception as e:
            print(f"\n❌ Error: {e}")
        finally:
            self.client.loop_stop()
            self.client.disconnect()
            print("✅ Simulator stopped")


def main():
    """Main function"""
    simulator = ESP32Simulator()
    simulator.run()


if __name__ == "__main__":
    main()

