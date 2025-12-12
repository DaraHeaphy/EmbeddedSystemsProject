# MQTT Testing Summary

Quick reference for testing the complete MQTT system.

## 🎯 System Overview

```
ESP32 (Publisher) → Python Broker → Python Client (Subscriber)
```

## 🚀 Quick Start

### 1. Start Broker
```bash
cd cloud
python mqtt_broker.py
```

### 2. Start Subscriber
```bash
cd cloud
python mqtt_test_client.py
```

### 3. Test with ESP32 Simulator (No Hardware Needed!)
```bash
cd cloud
python esp32_simulator.py
```

This simulates the ESP32 publishing telemetry continuously!

### 4. Configure & Flash Real ESP32 (Optional)
Edit `embedded/ESP32-reactor/src/main_with_cloud.c`:
- Set WiFi SSID/password
- Set MQTT broker URI to your PC's IP

```bash
cd embedded/ESP32-reactor
pio run --target upload
pio device monitor
```

## 📝 Files Created

### ESP32 Files
- `src/cloud/mqtt_handler.c/h` - MQTT client (publisher)
- `src/cloud/wifi_manager.c/h` - WiFi connection
- `src/cloud/cloud_publisher.c/h` - Telemetry→JSON converter
- `src/main_with_cloud.c` - Integrated main with cloud

### Cloud/Testing Files
- `cloud/mqtt_broker.py` - Custom MQTT broker
- `cloud/mqtt_test_client.py` - Subscriber test client
- `cloud/test_broker.py` - Simple test publisher
- `cloud/esp32_simulator.py` - **Full ESP32 simulator (no hardware needed!)**
- `cloud/start_broker.bat` - Quick start broker
- `cloud/start_test_client.bat` - Quick start client
- `cloud/start_simulator.bat` - Quick start ESP32 simulator

## 🔧 Key Configuration

**ESP32** (`main_with_cloud.c`):
```c
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"  
#define MQTT_BROKER_URI "mqtt://192.168.1.100:1883"
```

**Test Client** (`mqtt_test_client.py`):
```python
BROKER_HOST = "localhost"
TOPICS = ["reactor/telemetry", "reactor/alerts", "reactor/#"]
```

## ✅ Success Indicators

1. **Broker**: Shows "Client connected: reactor_core_001"
2. **Broker**: Shows "📨 PUBLISH from reactor_core_001"
3. **Client**: Displays JSON telemetry data
4. **ESP32**: Serial shows "MQTT_EVENT_CONNECTED"

## 🐛 Common Issues

| Issue | Solution |
|-------|----------|
| ESP32 can't connect to WiFi | Check SSID/password, use 2.4GHz |
| Can't connect to broker | Check PC IP, open firewall port 1883 |
| No messages in client | Verify topics match, test with test_broker.py |
| Broker connection refused | Make sure broker is running first |

## 📊 Data Flow

ESP32 publishes every 1 second:
```json
{
  "sample_id": 42,
  "temp": 25.30,
  "accel_mag": 9.810,
  "state": "NORMAL",
  "power": 75
}
```

## 🎯 Next Steps

1. ✅ Test Python broker ← YOU ARE HERE
2. ✅ Test ESP32 publisher
3. ⬜ Add authentication
4. ⬜ Deploy to production server
5. ⬜ Create data dashboard

