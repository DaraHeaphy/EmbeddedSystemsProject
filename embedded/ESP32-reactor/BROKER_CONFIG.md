# 🔧 MQTT Broker Configuration for ESP32

## Quick Configuration

### 1. Open your main file
`src/main_with_cloud.c`

### 2. Update these defines (lines 47-52):

```c
// WiFi credentials
#define WIFI_SSID       "YourWiFiName"
#define WIFI_PASSWORD   "YourWiFiPassword"

// MQTT broker configuration
#define MQTT_BROKER_URI "mqtt://alderaan.software-engineering.ie:1883"
#define MQTT_CLIENT_ID  "reactor_esp32_YOUR_ID"     // Make this unique!
#define MQTT_TOPIC      "students/YOUR_ID/sensors"  // Customize your topic
```

### 3. If authentication is required:

Uncomment and set:
```c
#define MQTT_USERNAME   "your_username"
#define MQTT_PASSWORD   "your_password"
```

---

## Understanding the Configuration

### Broker URI
```c
#define MQTT_BROKER_URI "mqtt://alderaan.software-engineering.ie:1883"
```
- `mqtt://` = protocol (plain MQTT, no TLS)
- `alderaan.software-engineering.ie` = broker hostname
- `:1883` = port (standard MQTT port)

### Client ID
```c
#define MQTT_CLIENT_ID  "reactor_esp32_001"
```
- Must be **unique** for each device
- Suggested format: `reactor_esp32_YOUR_NAME` or `reactor_esp32_001`, `002`, etc.
- If two clients use the same ID, they will keep disconnecting each other!

### Topic
```c
#define MQTT_TOPIC      "reactor/sensors"
```
- Where your sensor data will be published
- Recommended format: `students/YOUR_ID/sensors` or `reactor/DEVICE_ID/sensors`
- This is your **default topic** for publishing

---

## Topic Organization

### Good Topic Structure

```
students/
  ├── alice/
  │   ├── sensors       ← Main sensor data
  │   ├── telemetry     ← System telemetry
  │   ├── alerts        ← Alerts/warnings
  │   └── status        ← Device status
  │
  ├── bob/
  │   ├── sensors
  │   └── ...
```

### Example Configuration

```c
// For student "alice" with device "esp32_001"
#define MQTT_CLIENT_ID  "alice_esp32_001"
#define MQTT_TOPIC      "students/alice/sensors"
```

Then in your Python subscriber:
```python
TOPICS = [
    "students/alice/#",  # Subscribe to all alice's topics
]
```

---

## Publishing Examples

### Default Topic
```c
// Publishes to MQTT_TOPIC ("reactor/sensors")
char json[256];
snprintf(json, sizeof(json), "{\"temp\":%.2f,\"accel_x\":%.3f}", temp, accel_x);
mqtt_handler_publish_json(json);
```

### Specific Topic
```c
// Publish to a different topic
mqtt_handler_publish_json_to_topic("reactor/alerts", "{\"status\":\"warning\"}");
mqtt_handler_publish_json_to_topic("reactor/status", "{\"online\":true}");
```

---

## Testing Workflow

### Step 1: Local Testing (Optional)
```c
// Test with local broker first (your PC)
#define MQTT_BROKER_URI "mqtt://192.168.1.100:1883"  // Your PC's IP
```

Run local broker on your PC:
```bash
cd cloud/server
python mqtt_broker.py
```

### Step 2: Production Broker
```c
// Switch to alderaan broker
#define MQTT_BROKER_URI "mqtt://alderaan.software-engineering.ie:1883"
```

### Step 3: Verify with Python
```bash
cd cloud/testing
python test_subscriber.py
```

---

## Common Issues

### ❌ "Failed to connect to MQTT broker"
- Check WiFi credentials
- Verify broker URI is correct
- Test broker connectivity: `python cloud/testing/test_connectivity.py`
- Check if firewall is blocking port 1883

### ❌ "Client keeps disconnecting"
- Another device is using the same `MQTT_CLIENT_ID`
- Make your client ID unique!

### ❌ "Authentication failed"
- Broker requires username/password
- Uncomment and set `MQTT_USERNAME` and `MQTT_PASSWORD`

### ❌ "Messages not received in Python"
- Check topic names match exactly (case-sensitive!)
- Verify Python subscriber is subscribed to correct topics
- Check ESP32 logs: `ESP_LOGI` messages show if publishing succeeds

---

## Monitoring ESP32

### Serial Monitor
You'll see logs like:
```
I (12345) MQTT_HANDLER: MQTT_EVENT_CONNECTED
I (12567) MQTT_HANDLER: Published to reactor/sensors: {"temp":25.3,...}
I (12890) MQTT_HANDLER: MQTT_EVENT_PUBLISHED, msg_id=1
```

### Success indicators:
- ✅ `WiFi connected!`
- ✅ `MQTT_EVENT_CONNECTED`
- ✅ `Published to reactor/sensors`
- ✅ `MQTT_EVENT_PUBLISHED`

---

## Full Example Configuration

```c
// ============================================================================
// Configuration for Student "Alice" with ESP32 device #001
// ============================================================================
#define WIFI_SSID       "UniversityWiFi"
#define WIFI_PASSWORD   "wifi_password_here"

#define MQTT_BROKER_URI "mqtt://alderaan.software-engineering.ie:1883"
#define MQTT_CLIENT_ID  "alice_esp32_001"
#define MQTT_TOPIC      "students/alice/sensors"

// If authentication is required:
// #define MQTT_USERNAME   "alice"
// #define MQTT_PASSWORD   "secret_password"
```

Then in Python:
```python
# cloud/testing/test_subscriber.py
TOPICS = [
    "students/alice/#",  # All alice's topics
]
```

---

## Need Help?

1. **Test connectivity:** `python cloud/testing/test_connectivity.py`
2. **Read testing guide:** `cloud/testing/TESTING_GUIDE.md`
3. **Check broker status:** Ask instructor about alderaan broker
4. **Debug:** Enable verbose ESP32 logs in `sdkconfig`

---

**Happy Publishing! 🚀**

