# ESP32 Reactor Cloud Publishing - Testing Guide

Complete guide to test the MQTT publisher from ESP32 to your custom Python broker.

## 📋 What You Have

1. **Custom MQTT Broker** - `cloud/mqtt_broker.py`
2. **Test Subscriber** - `cloud/mqtt_test_client.py`  
3. **ESP32 Publisher** - ESP32 firmware with MQTT
4. **Test Publisher** - `cloud/test_broker.py` (for testing broker)

## 🎯 Complete Testing Flow

```
┌─────────────┐         ┌─────────────┐         ┌──────────────┐
│   ESP32     │  MQTT   │   Custom    │  MQTT   │  Test Client │
│  Publisher  │────────>│   Broker    │────────>│ (Subscriber) │
│ (Hardware)  │         │  (Python)   │         │   (Python)   │
└─────────────┘         └─────────────┘         └──────────────┘
```

---

## 🚀 Step-by-Step Testing

### Step 1: Configure ESP32 WiFi & MQTT

Edit `src/main_with_cloud.c` (lines 33-37):

```c
#define WIFI_SSID       "YourWiFiName"      // ← Your WiFi name
#define WIFI_PASSWORD   "YourWiFiPassword"  // ← Your WiFi password
#define MQTT_BROKER_URI "mqtt://192.168.1.100:1883"  // ← Your PC's IP
#define MQTT_CLIENT_ID  "reactor_core_001"
#define MQTT_TOPIC      "reactor/telemetry"
```

**Finding Your PC's IP:**

Windows PowerShell:
```powershell
ipconfig
# Look for "IPv4 Address" under your WiFi adapter
```

### Step 2: Enable Cloud Code in Main

**Option A - Rename files (Quick):**
```bash
cd embedded/ESP32-reactor/src
mv main.c main_uart_only.c
mv main_with_cloud.c main.c
```

**Option B - Manually integrate cloud code into your existing main.c**

### Step 3: Build & Flash ESP32

```bash
cd embedded/ESP32-reactor
pio run --target upload
pio device monitor  # Watch serial output
```

Expected output:
```
I (123) reactor: ESP32 reactor starting up
I (234) WIFI_MGR: WiFi initialized, connecting to SSID: YourWiFiName
I (567) WIFI_MGR: Got IP: 192.168.1.50
I (789) MQTT_HANDLER: MQTT handler initialized
I (890) MQTT_HANDLER: Connecting to MQTT broker...
I (901) MQTT_HANDLER: MQTT_EVENT_CONNECTED
I (1000) reactor: All tasks created, system running
```

### Step 4: Start Your Broker

Terminal 1:
```powershell
cd cloud
python mqtt_broker.py
```

Expected output:
```
============================================================
 Custom MQTT Broker Started
============================================================
 Listening on 0.0.0.0:1883
 Waiting for connections... (Press Ctrl+C to stop)
```

### Step 5: Start Test Subscriber

Terminal 2:
```powershell
cd cloud
python mqtt_test_client.py
```

Expected output:
```
============================================================
 MQTT Test Client for ESP32 Reactor Publisher
============================================================

🔌 Connecting to broker at localhost:1883...
 Connected to MQTT broker at localhost:1883
📡 Subscribing to topics:
   - reactor/sensors
   - reactor/telemetry
   - reactor/alerts
   - reactor/#

 Waiting for messages... (Press Ctrl+C to exit)
```

### Step 6: Watch the Data Flow! 🎉

When ESP32 publishes, you'll see:

**In Broker Terminal:**
```
 New connection from ('192.168.1.50', 52341)
 Client connected: reactor_core_001 (('192.168.1.50', 52341))
📨 PUBLISH from reactor_core_001: topic='reactor/telemetry', payload=b'{"sample_id":0,"temp":25.30,"accel_mag":9.810,'...
```

**In Test Client Terminal:**
```
[14:23:45.123] 📩 Topic: reactor/telemetry
           Data: {
                      "sample_id": 42,
                      "temp": 25.30,
                      "accel_mag": 9.810,
                      "state": "NORMAL",
                      "power": 75
                  }
```

---

## 🔧 Troubleshooting

### ESP32 Can't Connect to WiFi

**Check:**
- SSID and password are correct (case-sensitive!)
- ESP32 and PC are on the same network
- 2.4GHz WiFi (ESP32 doesn't support 5GHz)

**Serial Monitor:**
```
E (1234) WIFI_MGR: WiFi disconnected, reconnecting...
```

### ESP32 Can't Connect to MQTT Broker

**Check:**
1. Broker is running: `python mqtt_broker.py`
2. PC's IP address is correct in `MQTT_BROKER_URI`
3. Firewall allows port 1883

**Open Windows Firewall (Run as Admin):**
```powershell
New-NetFirewallRule -DisplayName "MQTT Broker" -Direction Inbound -Protocol TCP -LocalPort 1883 -Action Allow
```

**Serial Monitor:**
```
E (2345) MQTT_HANDLER: Failed to connect to MQTT broker
```

### No Messages in Test Client

**Check:**
- Broker shows ESP32 connected: ` Client connected: reactor_core_001`
- Broker shows PUBLISH messages: `📨 PUBLISH from reactor_core_001`
- Test client subscribed to correct topics

**Test broker with Python script:**
```bash
cd cloud
python test_broker.py  # Publishes test messages
```

### Messages Delayed or Slow

The ESP32 only publishes every 10th sample (1 Hz) to reduce traffic.

Change in `main_with_cloud.c` line 79:
```c
// Publish every sample instead of every 10th
if (sample_id % 1 == 0) {  // Change from % 10
```

---

## 📊 Data Format

ESP32 publishes JSON telemetry:

```json
{
  "sample_id": 42,
  "temp": 25.30,
  "accel_mag": 9.810,
  "state": "NORMAL",
  "power": 75
}
```

**Fields:**
- `sample_id` - Incrementing sample counter
- `temp` - Temperature in Celsius
- `accel_mag` - Accelerometer magnitude
- `state` - "NORMAL", "WARNING", or "SCRAM"
- `power` - Power percentage (0-100)

---

## 🎓 Next Steps

Once basic publishing works:

1. **Add Authentication** - Require username/password
2. **Add TLS/SSL** - Encrypt MQTT traffic
3. **Deploy Broker** - Run on `alderaan.software-engineering.ie`
4. **Add Persistence** - Store messages to database
5. **Create Dashboard** - Visualize real-time data

---

## 📝 Quick Reference

**Start Everything:**
```powershell
# Terminal 1 - Broker
cd cloud
python mqtt_broker.py

# Terminal 2 - Subscriber
cd cloud
python mqtt_test_client.py

# Terminal 3 - ESP32 Monitor
cd embedded/ESP32-reactor
pio device monitor
```

**Test Without ESP32:**
```powershell
# Terminal 3 - Test Publisher (simulates ESP32)
cd cloud
python test_broker.py
```

---

## 🐛 Debug Mode

Enable verbose MQTT logging in ESP32:

Edit `mqtt_handler.c`, change log level:
```c
static const char* TAG = "MQTT_HANDLER";
// Add at top of file:
esp_log_level_set("MQTT_HANDLER", ESP_LOG_DEBUG);
```

Rebuild and flash to see detailed MQTT packet info.

---

## ✅ Success Checklist

- [ ] Broker starts and listens on port 1883
- [ ] Test client connects to broker
- [ ] ESP32 connects to WiFi (see IP in serial monitor)
- [ ] ESP32 connects to MQTT broker (broker shows client connection)
- [ ] Broker shows PUBLISH messages from ESP32
- [ ] Test client displays JSON telemetry data
- [ ] Data updates in real-time (every ~1 second)

---

**You're all set! Your ESP32 is now publishing reactor telemetry to the cloud! 🚀**

