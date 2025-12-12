# Reactor Cloud Infrastructure

MQTT-based telemetry system for ESP32 reactor monitoring.

## Quick Start

```bash
cd scripts
start_all.bat
```

Opens dashboard at: **http://localhost:5000**

## Directory Structure

```
cloud/
├── requirements.txt        # Python dependencies
├── server/                 # Backend services
│   ├── mqtt_broker.py      # Custom MQTT broker
│   └── web_dashboard.py    # Web dashboard server
├── testing/                # Testing & simulation
│   ├── esp32_simulator.py  # ESP32 telemetry simulator
│   ├── mqtt_test_client.py # MQTT subscriber test client
│   └── test_broker.py      # Simple test publisher
├── scripts/                # Startup scripts
│   ├── start_all.bat       # Launch complete system
│   ├── start_broker.bat    # Launch broker only
│   ├── start_dashboard.bat # Launch dashboard only
│   └── start_simulator.bat # Launch simulator only
└── docs/                   # Documentation
    ├── QUICK_START.md      # Quick start guide
    ├── TESTING_SUMMARY.md  # Testing reference
    └── README.md           # Detailed documentation
```

## Installation

```bash
pip install -r requirements.txt
```

## System Architecture

```
ESP32 → MQTT Broker → Web Dashboard → Browser
        (Port 1883)   (Port 5000)
```

## Components

### Server
- **MQTT Broker**: Custom Python MQTT 3.1.1 broker
- **Web Dashboard**: Flask + WebSocket real-time UI

### Testing
- **ESP32 Simulator**: Publishes realistic telemetry without hardware
- **Test Client**: Command-line MQTT subscriber
- **Test Broker**: Simple test message publisher

## Features

- Real-time telemetry display (temperature, acceleration, power)
- Visual gauges (thermometer, dial, power bar)
- State indicators (NORMAL/WARNING/SCRAM)
- 1960s Soviet control panel aesthetic
- WebSocket-based live updates

## Usage

See `docs/QUICK_START.md` for detailed instructions.

