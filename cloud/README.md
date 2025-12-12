## Cloud

Contains cloud server implementation to live on alderaan.software-engineering.ie

---

## 🧪 Local MQTT Testing Setup

This guide helps you test the ESP32 MQTT publisher (`embedded/ESP32-reactor/src/cloud/mqtt_handler.c`) locally before deploying to production.

### Prerequisites

1. **Mosquitto MQTT Broker** (local MQTT server)
2. **Python 3.7+** with `paho-mqtt` library

---

## 📦 Installation

### Install Python Dependencies (Broker is Python-based!)

```bash
cd cloud
pip install -r requirements.txt
```

---

## 🚀 Running the Test Environment

### Step 1: Start Your Custom MQTT Broker

**Option 1 - Using the batch file (Windows):**
```powershell
cd cloud
.\start_broker.bat
```

**Option 2 - Direct Python:**
```bash
cd cloud
python mqtt_broker.py
```

You should see output like:
```
============================================================
🚀 Custom MQTT Broker Started
============================================================
📡 Listening on 0.0.0.0:1883
⏳ Waiting for connections... (Press Ctrl+C to stop)
```

### Step 2: Start the Test Subscriber

In a **new terminal/PowerShell window**:

```bash
cd cloud
python mqtt_test_client.py
```

You should see:
```
============================================================
🧪 MQTT Test Client for ESP32 Reactor Publisher
============================================================

🔌 Connecting to broker at localhost:1883...
✅ Connected to MQTT broker at localhost:1883
📡 Subscribing to topics:
   - reactor/sensors
   - reactor/telemetry
   - reactor/alerts
   - reactor/#

⏳ Waiting for messages... (Press Ctrl+C to exit)
```

### Step 3: Configure & Flash ESP32

1. **Configure WiFi** in your ESP32 code (you'll need to add WiFi initialization)
2. **Configure MQTT broker address** to your computer's IP:
   ```c
   mqtt_config_t config = {
       .broker_uri = "mqtt://192.168.1.XXX:1883",  // Your PC's IP
       .client_id = "reactor_core_001",
       .default_topic = "reactor/sensors"
   };
   ```
3. **Flash and run** the ESP32

### Step 4: Watch the Messages!

When your ESP32 publishes data, you'll see it in the test client:

```
[14:23:45.123] 📩 Topic: reactor/sensors
           Data: {
                      "temp": 72.5,
                      "accel_x": 0.12,
                      "accel_y": 0.05,
                      "accel_z": 9.81,
                      "sample_id": 42
                  }
```

---

## 🔍 Manual Testing

### Option 1: With Mosquitto CLI (if installed)

**Subscribe to all reactor topics:**
```bash
mosquitto_sub -h localhost -t "reactor/#" -v
```

**Publish a test message (simulate ESP32):**
```bash
mosquitto_pub -h localhost -t "reactor/sensors" -m '{"temp":25.5,"status":"ok"}'
```

### Option 2: ESP32 Simulator (Recommended!)

**Full ESP32 simulation without hardware:**
```bash
cd cloud
python esp32_simulator.py
```

This continuously publishes realistic reactor telemetry data, simulating temperature changes, state transitions (NORMAL→WARNING→SCRAM), and power adjustments. Perfect for testing your entire MQTT pipeline!

### Option 3: Simple Python Test

Create a test publisher:
```python
import paho.mqtt.client as mqtt

client = mqtt.Client("test_publisher")
client.connect("localhost", 1883)
client.publish("reactor/sensors", '{"temp":25.5,"status":"ok"}')
client.disconnect()
```

---

## 🌐 Network Configuration

### Finding Your PC's IP Address

**Windows:**
```powershell
ipconfig
# Look for "IPv4 Address" under your active network adapter
```

**macOS/Linux:**
```bash
ifconfig | grep "inet "
# or
ip addr show
```

### Firewall Rules

Make sure port **1883** is open on your PC's firewall for the ESP32 to connect:

**Windows Firewall:**
```powershell
# Run as Administrator
New-NetFirewallRule -DisplayName "Custom MQTT Broker" -Direction Inbound -Protocol TCP -LocalPort 1883 -Action Allow
```

---

## 🔧 Custom Broker Features

Your custom Python MQTT broker (`mqtt_broker.py`) supports:

✅ **MQTT 3.1.1 Protocol**
- CONNECT/CONNACK - Client connections
- PUBLISH/PUBACK - Message publishing (QoS 0 and 1)
- SUBSCRIBE/SUBACK - Topic subscriptions
- PINGREQ/PINGRESP - Keep-alive
- DISCONNECT - Clean disconnection

✅ **Wildcard Subscriptions**
- Single level: `reactor/+/temp`
- Multi level: `reactor/#`

✅ **Multiple Clients**
- Thread-based handling
- Concurrent connections

✅ **Real-time Logging**
- See all connections, publishes, subscribes

### Customizing the Broker

Edit `mqtt_broker.py` to add features:
- Authentication (username/password)
- Message persistence
- QoS 2 support
- WebSocket support
- Retained messages
- Session management

---

## 📊 Expected Topics

Based on your ESP32 telemetry structure, you might publish to:

- `reactor/sensors` - General sensor data (temp, accel, etc.)
- `reactor/telemetry` - Full telemetry frames
- `reactor/status` - Status updates
- `reactor/alerts` - Critical alerts

Customize the topics in `mqtt_test_client.py` as needed!

---

## 🐛 Troubleshooting

**"Connection Refused" Error:**
- Ensure Mosquitto is running: `mosquitto -v`
- Check if port 1883 is in use: `netstat -an | findstr 1883` (Windows)

**ESP32 Can't Connect:**
- Verify your PC's IP address
- Check firewall allows port 1883
- Ensure ESP32 and PC are on the same network
- Try `mqtt://broker.hivemq.com` for public broker testing first

**No Messages Appearing:**
- Check ESP32 serial output for MQTT connection status
- Verify the topic names match between publisher and subscriber
- Use `mosquitto_sub -h localhost -t "#" -v` to see ALL topics

---

## 🎯 Next Steps

Once local testing works:
1. Deploy MQTT broker to `alderaan.software-engineering.ie`
2. Update ESP32 broker URI to production server
3. Implement authentication (username/password)
4. Add TLS/SSL encryption for production
5. Set up persistent message storage