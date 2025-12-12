# Quick Start Guide

Run the complete system with one command.

## Installation

```bash
cd cloud
pip install -r requirements.txt
```

## Run Everything

### Windows
```bash
start_all.bat
```

### Linux/Mac
```bash
python start_all.py
```

This starts:
1. **MQTT Broker** (port 1883)
2. **ESP32 Simulator** (publishes telemetry every 1s)
3. **Web Dashboard Backend** (port 5000)

## Access Dashboard

Open browser: **http://localhost:5000**

You'll see a simple status page showing:
- MQTT connection status
- Messages received count

## Full React Dashboard (Optional)

For the complete UI with charts:

```bash
cd dashboard
npm install
npm start
```

Opens at: **http://localhost:3000**

## What You'll See

The simulator publishes telemetry every second:
```json
{
  "sample_id": 42,
  "temp": 72.5,
  "accel_mag": 9.810,
  "state": "NORMAL",
  "power": 75
}
```

The dashboard receives it via WebSocket in real-time.

## Stop Everything

**Windows:** Close the 3 console windows

**Linux/Mac:** Press Ctrl+C in the terminal

## Architecture

```
ESP32 Simulator → MQTT Broker → Web Dashboard
    (Publisher)    (Port 1883)    (WebSocket Server)
                                        ↓
                                   Browser Client
                                   (Port 5000)
```

All communication happens locally on your machine.

