#!/usr/bin/env python3
"""
MQTT Web Dashboard Backend
Bridges MQTT broker to WebSocket for web clients
"""

from flask import Flask, send_from_directory
from flask_socketio import SocketIO
from flask_cors import CORS
import paho.mqtt.client as mqtt
import json
import threading
from datetime import datetime
import os

MQTT_BROKER_HOST = "localhost"
MQTT_BROKER_PORT = 1883
MQTT_CLIENT_ID = "web_dashboard"
MQTT_TOPICS = ["reactor/#"]

app = Flask(__name__, static_folder='dashboard/build')
CORS(app)
app.config['SECRET_KEY'] = 'reactor_secret_key'
socketio = SocketIO(app, cors_allowed_origins="*")

mqtt_client = None
mqtt_connected = False

latest_telemetry = {}
stats = {
    "messages_received": 0,
    "connection_time": None,
    "uptime": 0
}

def on_mqtt_connect(client, userdata, flags, rc):
    global mqtt_connected
    if rc == 0:
        mqtt_connected = True
        stats["connection_time"] = datetime.now().isoformat()
        
        for topic in MQTT_TOPICS:
            client.subscribe(topic)
        
        socketio.emit('mqtt_status', {'connected': True})
    else:
        mqtt_connected = False
        socketio.emit('mqtt_status', {'connected': False, 'error': rc})

def on_mqtt_disconnect(client, userdata, rc):
    global mqtt_connected
    mqtt_connected = False
    socketio.emit('mqtt_status', {'connected': False})

def on_mqtt_message(client, userdata, msg):
    global latest_telemetry, stats
    
    stats["messages_received"] += 1
    timestamp = datetime.now().isoformat()
    
    try:
        raw_payload = msg.payload.decode('utf-8')
        payload = json.loads(raw_payload)
        
        data = {
            'topic': msg.topic,
            'payload': payload,
            'timestamp': timestamp
        }
        
        if msg.topic == "reactor/telemetry":
            latest_telemetry = data
            socketio.emit('telemetry', data)
        elif msg.topic == "reactor/alerts":
            socketio.emit('alert', data)
        else:
            socketio.emit('message', data)
    
    except json.JSONDecodeError as e:
        print(f"JSON decode error: {e}")
        print(f"Raw payload: {raw_payload}")
    except Exception as e:
        print(f"Error processing message: {e}")

def init_mqtt():
    global mqtt_client
    
    mqtt_client = mqtt.Client(MQTT_CLIENT_ID)
    mqtt_client.on_connect = on_mqtt_connect
    mqtt_client.on_disconnect = on_mqtt_disconnect
    mqtt_client.on_message = on_mqtt_message
    
    try:
        mqtt_client.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60)
        mqtt_client.loop_start()
    except Exception as e:
        print(f"MQTT connection failed: {e}")

@app.route('/')
def index():
    if os.path.exists(os.path.join(app.static_folder, 'index.html')):
        return send_from_directory(app.static_folder, 'index.html')
    else:
        return """
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Reactor Telemetry</title>
    <script src="https://cdn.socket.io/4.5.4/socket.io.min.js"></script>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Courier New', Consolas, monospace;
            background: #1a1a1a;
            color: #d4a574;
            display: flex;
            flex-direction: column;
            min-height: 100vh;
            background-image: 
                repeating-linear-gradient(0deg, rgba(0, 0, 0, 0.15) 0px, transparent 1px, transparent 2px, rgba(0, 0, 0, 0.15) 3px),
                repeating-linear-gradient(90deg, rgba(0, 0, 0, 0.15) 0px, transparent 1px, transparent 2px, rgba(0, 0, 0, 0.15) 3px);
            background-size: 4px 4px;
        }
        
        header {
            background: #0a0a0a;
            padding: 1rem 2rem;
            border-bottom: 4px solid #4a4a4a;
            box-shadow: 0 4px 0 #2a2a2a, inset 0 -2px 10px rgba(0, 0, 0, 0.5);
            display: flex;
            justify-content: space-between;
            align-items: center;
            position: relative;
            z-index: 10;
        }
        
        h1 {
            font-size: 1.4rem;
            font-weight: 700;
            text-transform: uppercase;
            letter-spacing: 4px;
            color: #d4a574;
            text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.8);
        }
        
        h1::before {
            content: '///';
            margin-right: 1rem;
            color: #8b7355;
        }
        
        h1::after {
            content: '///';
            margin-left: 1rem;
            color: #8b7355;
        }
        
        .status {
            display: flex;
            gap: 1.5rem;
            align-items: center;
            font-family: 'Courier New', monospace;
            position: relative;
            z-index: 10;
        }
        
        .status-badge {
            padding: 0.5rem 1rem;
            background: #0a0a0a;
            border: 2px solid;
            font-size: 0.8rem;
            font-weight: 700;
            letter-spacing: 2px;
            box-shadow: inset 2px 2px 4px rgba(0, 0, 0, 0.8);
        }
        
        .connected {
            color: #8ba574;
            border-color: #5a6a4a;
        }
        
        .disconnected {
            color: #a57474;
            border-color: #6a4a4a;
        }
        
        main {
            flex: 1;
            padding: 0.25rem 2rem 2rem;
            max-width: 1400px;
            margin: 0 auto;
            width: 100%;
            position: relative;
            z-index: 10;
        }
        
        .telemetry-grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 1rem;
            max-width: 100%;
            margin-top: 0.5rem;
        }
        
        .metric-card {
            background: #0f0f0f;
            border: 3px solid #3a3a3a;
            padding: 1rem;
            position: relative;
            box-shadow: 
                inset 2px 2px 8px rgba(0, 0, 0, 0.8),
                2px 2px 0 #2a2a2a;
        }
        
        .metric-card::after {
            content: '';
            position: absolute;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: repeating-linear-gradient(
                0deg,
                transparent,
                transparent 2px,
                rgba(0, 0, 0, 0.1) 2px,
                rgba(0, 0, 0, 0.1) 4px
            );
            pointer-events: none;
        }
        
        .metric-label {
            font-size: 0.7rem;
            color: #8b7355;
            text-transform: uppercase;
            letter-spacing: 2px;
            margin-bottom: 0.75rem;
            font-weight: 700;
            border-bottom: 1px solid #2a2a2a;
            padding-bottom: 0.5rem;
        }
        
        .metric-label::before {
            content: '> ';
            color: #6a5a4a;
        }
        
        .metric-value {
            font-size: 2rem;
            font-weight: 700;
            font-family: 'Courier New', monospace;
            color: #d4a574;
            line-height: 1;
            letter-spacing: 2px;
            text-shadow: 1px 1px 2px rgba(0, 0, 0, 0.8);
        }
        
        .metric-unit {
            font-size: 1rem;
            color: #8b7355;
            margin-left: 0.5rem;
        }
        
        .metric-sublabel {
            font-size: 0.65rem;
            color: #5a5a5a;
            margin-top: 0.75rem;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        
        .state-card {
            grid-column: 1 / -1;
        }
        
        .state-indicators {
            display: flex;
            justify-content: center;
            align-items: center;
            gap: 3rem;
            margin-top: 0.5rem;
            margin-bottom: 0.5rem;
            max-width: 800px;
            margin-left: auto;
            margin-right: auto;
        }
        
        .state-indicator {
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 0.75rem;
        }
        
        .state-light {
            width: 70px;
            height: 70px;
            border-radius: 50%;
            border: 4px solid #2a2a2a;
            box-shadow: 
                inset 3px 3px 10px rgba(0, 0, 0, 0.9),
                2px 2px 0 rgba(0, 0, 0, 0.5);
            position: relative;
            transition: all 0.3s ease;
        }
        
        .state-light::after {
            content: '';
            position: absolute;
            top: 20%;
            left: 20%;
            width: 30%;
            height: 30%;
            border-radius: 50%;
            background: rgba(255, 255, 255, 0.1);
        }
        
        .state-light.active {
            border-width: 6px;
        }
        
        .state-light.normal {
            background: #1a1a1a;
            border-color: #2a2a2a;
        }
        
        .state-light.normal.active {
            background: #8ba574;
            border-color: #5a6a4a;
            box-shadow: 
                0 0 20px rgba(139, 165, 116, 0.6),
                inset 2px 2px 8px rgba(0, 0, 0, 0.5);
        }
        
        .state-light.warning {
            background: #1a1a1a;
            border-color: #2a2a2a;
        }
        
        .state-light.warning.active {
            background: #d4a574;
            border-color: #8b7355;
            box-shadow: 
                0 0 20px rgba(212, 165, 116, 0.6),
                inset 2px 2px 8px rgba(0, 0, 0, 0.5);
        }
        
        .state-light.scram {
            background: #1a1a1a;
            border-color: #2a2a2a;
        }
        
        .state-light.scram.active {
            background: #a57474;
            border-color: #6a4a4a;
            box-shadow: 
                0 0 20px rgba(165, 116, 116, 0.6),
                inset 2px 2px 8px rgba(0, 0, 0, 0.5);
        }
        
        .state-text {
            font-size: 1rem;
            font-weight: 700;
            letter-spacing: 2px;
            text-transform: uppercase;
            opacity: 0.3;
            transition: all 0.3s ease;
        }
        
        .state-text.active {
            opacity: 1;
            text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.8);
        }
        
        .state-text.normal {
            color: #8ba574;
        }
        
        .state-text.warning {
            color: #d4a574;
        }
        
        .state-text.scram {
            color: #a57474;
        }
        
        .no-data {
            text-align: center;
            padding: 3rem;
            color: #8b7355;
            font-style: normal;
            font-weight: 700;
            text-transform: uppercase;
            letter-spacing: 2px;
        }
        
        .dial-container {
            position: relative;
            width: 50%;
            padding-top: 50%;
            margin: 0.5rem auto 0;
            background: radial-gradient(circle, #1a1a1a 0%, #0a0a0a 100%);
            border-radius: 50%;
            border: 4px solid #2a2a2a;
            box-shadow: 
                inset 0 0 20px rgba(0, 0, 0, 0.9),
                inset 0 0 8px rgba(0, 0, 0, 0.5),
                0 2px 0 #1a1a1a;
        }
        
        .dial-svg {
            position: absolute;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
        }
        
        .dial-face {
            fill: none;
            stroke: #3a3a3a;
            stroke-width: 2;
        }
        
        .dial-tick {
            stroke: #5a5a5a;
            stroke-width: 2;
            stroke-linecap: round;
        }
        
        .dial-tick-major {
            stroke: #8b7355;
            stroke-width: 3;
        }
        
        .dial-number {
            fill: #8b7355;
            font-family: 'Courier New', monospace;
            font-size: 12px;
            font-weight: 700;
            text-anchor: middle;
        }
        
        .dial-needle {
            stroke: #d4a574;
            stroke-width: 3;
            stroke-linecap: round;
            filter: drop-shadow(2px 2px 2px rgba(0, 0, 0, 0.8));
            transition: transform 0.3s ease-out;
            transform-origin: center;
        }
        
        .dial-center {
            fill: #2a2a2a;
            stroke: #5a5a5a;
            stroke-width: 2;
        }
        
        .dial-label {
            fill: #6a5a4a;
            font-family: 'Courier New', monospace;
            font-size: 10px;
            text-transform: uppercase;
            letter-spacing: 2px;
            text-anchor: middle;
        }
        
        .dial-glass {
            fill: radial-gradient(circle at 30% 30%, rgba(255, 255, 255, 0.05), transparent);
            pointer-events: none;
        }
        
        .thermometer-container {
            width: 50%;
            margin: 0.5rem auto 0;
            position: relative;
        }
        
        .thermometer-svg {
            width: 100%;
            height: auto;
        }
        
        .thermometer-outline {
            fill: none;
            stroke: #3a3a3a;
            stroke-width: 3;
        }
        
        .thermometer-bulb {
            fill: #1a1a1a;
            stroke: #3a3a3a;
            stroke-width: 2;
        }
        
        .thermometer-tube {
            fill: #0a0a0a;
            stroke: #3a3a3a;
            stroke-width: 2;
        }
        
        .thermometer-fill {
            fill: #d4a574;
            transition: height 0.3s ease-out;
        }
        
        .thermometer-tick {
            stroke: #5a5a5a;
            stroke-width: 1;
        }
        
        .thermometer-tick-major {
            stroke: #8b7355;
            stroke-width: 2;
        }
        
        .thermometer-number {
            fill: #8b7355;
            font-family: 'Courier New', monospace;
            font-size: 10px;
            font-weight: 700;
            text-anchor: end;
        }
        
        @media (max-width: 1200px) {
            .telemetry-grid {
                grid-template-columns: 1fr;
            }
            
            .state-indicators {
                gap: 2rem;
            }
            
            .state-light {
                width: 80px;
                height: 80px;
            }
        }
        
        @media (max-width: 768px) {
            .metric-value {
                font-size: 2rem;
            }
            
            .state-indicators {
                gap: 1.5rem;
            }
            
            .state-light {
                width: 60px;
                height: 60px;
            }
            
            .state-text {
                font-size: 1rem;
            }
        }
    </style>
</head>
<body>
    <header>
        <h1>Reactor Telemetry</h1>
        <div class="status">
            <span id="mqtt-status" class="status-badge disconnected">DISCONNECTED</span>
            <span style="color: #a8b2d1;">Messages: <span id="msg-count">0</span></span>
        </div>
    </header>
    
    <main>
        <div id="content"></div>
    </main>
    
    <script>
        const socket = io();
        let msgCount = 0;
        
        function renderDashboard(payload = null) {
            const currentState = payload?.state || 'NORMAL';
            
            const temp = payload?.temp ?? 0;
            const tempDisplay = temp.toFixed(2);
            const accel = payload?.accel_mag ?? 9.81;
            const accelDisplay = accel.toFixed(3);
            const power = payload?.power ?? 0;
            const sampleId = payload?.sample_id ?? 0;
            
            const minAccel = 0;
            const maxAccel = 25;
            const clampedAccel = Math.max(minAccel, Math.min(maxAccel, accel));
            const angle = ((clampedAccel - minAccel) / (maxAccel - minAccel)) * 270 - 135;
            
            const minTemp = 0;
            const maxTemp = 100;
            const clampedTemp = Math.max(minTemp, Math.min(maxTemp, temp));
            const tempPercent = ((clampedTemp - minTemp) / (maxTemp - minTemp)) * 100;
            
            document.getElementById('content').innerHTML = `
                <div class="metric-card state-card">
                    <div class="metric-label">System State</div>
                    <div class="state-indicators">
                        <div class="state-indicator">
                            <div class="state-light normal ${currentState === 'NORMAL' ? 'active' : ''}"></div>
                            <div class="state-text normal ${currentState === 'NORMAL' ? 'active' : ''}">NORMAL</div>
                        </div>
                        <div class="state-indicator">
                            <div class="state-light warning ${currentState === 'WARNING' ? 'active' : ''}"></div>
                            <div class="state-text warning ${currentState === 'WARNING' ? 'active' : ''}">WARNING</div>
                        </div>
                        <div class="state-indicator">
                            <div class="state-light scram ${currentState === 'SCRAM' ? 'active' : ''}"></div>
                            <div class="state-text scram ${currentState === 'SCRAM' ? 'active' : ''}">SCRAM</div>
                        </div>
                    </div>
                </div>
                
                <div class="telemetry-grid">
                    <div class="metric-card">
                        <div class="metric-label">Temperature</div>
                        <div class="metric-value">
                            ${tempDisplay}
                            <span class="metric-unit">°C</span>
                        </div>
                        <div class="thermometer-container">
                            <svg class="thermometer-svg" viewBox="0 0 100 160" preserveAspectRatio="xMidYMid meet">
                                <rect class="thermometer-tube" x="35" y="20" width="30" height="100" rx="15" />
                                
                                ${Array.from({length: 11}, (_, i) => {
                                    const value = 100 - (i * 10);
                                    const y = 20 + (i * 100 / 10);
                                    const isMajor = i % 2 === 0;
                                    const tickLength = isMajor ? 8 : 5;
                                    return `
                                        <line class="${isMajor ? 'thermometer-tick-major' : 'thermometer-tick'}" 
                                              x1="35" y1="${y}" x2="${35 - tickLength}" y2="${y}" />
                                        ${isMajor ? `<text class="thermometer-number" x="24" y="${y + 3}">${value}</text>` : ''}
                                    `;
                                }).join('')}
                                
                                <clipPath id="tube-clip">
                                    <rect x="35" y="20" width="30" height="100" rx="15" />
                                </clipPath>
                                
                                <rect class="thermometer-fill" 
                                      x="35" 
                                      y="${120 - tempPercent}" 
                                      width="30" 
                                      height="${tempPercent}" 
                                      clip-path="url(#tube-clip)" />
                                
                                <circle class="thermometer-bulb" cx="50" cy="135" r="15" />
                                
                                <circle class="thermometer-fill" cx="50" cy="135" r="12" />
                                
                                <rect class="thermometer-outline" x="35" y="20" width="30" height="100" rx="15" />
                                <circle class="thermometer-outline" cx="50" cy="135" r="15" />
                            </svg>
                        </div>
                        <div class="metric-sublabel">Sample #${sampleId}</div>
                    </div>
                    
                    <div class="metric-card">
                        <div class="metric-label">Acceleration</div>
                        <div class="metric-value">
                            ${accelDisplay}
                            <span class="metric-unit">m/s²</span>
                        </div>
                        <div class="dial-container">
                            <svg class="dial-svg" viewBox="0 0 200 200">
                                <defs>
                                    <radialGradient id="glassGrad" cx="30%" cy="30%">
                                        <stop offset="0%" style="stop-color:rgba(255,255,255,0.1)" />
                                        <stop offset="100%" style="stop-color:transparent" />
                                    </radialGradient>
                                </defs>
                                
                                <circle class="dial-face" cx="100" cy="100" r="85" />
                                
                                ${Array.from({length: 26}, (_, i) => {
                                    const angle = (i * 270 / 25) - 135;
                                    const isMajor = i % 5 === 0;
                                    const length = isMajor ? 15 : 10;
                                    const rad = (angle * Math.PI) / 180;
                                    const innerRadius = 70;
                                    const x1 = 100 + Math.cos(rad) * innerRadius;
                                    const y1 = 100 + Math.sin(rad) * innerRadius;
                                    const x2 = 100 + Math.cos(rad) * (innerRadius - length);
                                    const y2 = 100 + Math.sin(rad) * (innerRadius - length);
                                    return `<line class="${isMajor ? 'dial-tick-major' : 'dial-tick'}" 
                                                  x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" />`;
                                }).join('')}
                                
                                ${[0, 5, 10, 15, 20, 25].map((val, i) => {
                                    const angle = (i * 270 / 5) - 135;
                                    const rad = (angle * Math.PI) / 180;
                                    const x = 100 + Math.cos(rad) * 50;
                                    const y = 100 + Math.sin(rad) * 50;
                                    return `<text class="dial-number" x="${x}" y="${y + 4}">${val}</text>`;
                                }).join('')}
                                                                
                                <line id="dial-needle" class="dial-needle" 
                                      x1="100" y1="100" x2="100" y2="30"
                                      style="transform: rotate(${angle}deg)" />
                                
                                <circle class="dial-center" cx="100" cy="100" r="6" />
                                
                                <circle class="dial-glass" cx="100" cy="100" r="85" fill="url(#glassGrad)" />
                            </svg>
                        </div>
                        <div class="metric-sublabel">Magnitude</div>
                    </div>
                    
                    <div class="metric-card">
                        <div class="metric-label">Power Output</div>
                        <div class="metric-value">
                            ${power}
                            <span class="metric-unit">%</span>
                        </div>
                        <div class="metric-sublabel">Current Level</div>
                    </div>
                </div>
            `;
        }
        
        // Initialize dashboard with default values
        renderDashboard();
        
        socket.on('connect', () => {
            console.log('Connected to server');
        });
        
        socket.on('mqtt_status', (status) => {
            const badge = document.getElementById('mqtt-status');
            if (status.connected) {
                badge.textContent = 'CONNECTED';
                badge.className = 'status-badge connected';
            } else {
                badge.textContent = 'DISCONNECTED';
                badge.className = 'status-badge disconnected';
            }
        });
        
        socket.on('telemetry', (data) => {
            msgCount++;
            document.getElementById('msg-count').textContent = msgCount;
            
            const payload = data.payload;
            
            if (!payload || typeof payload !== 'object') {
                console.error('Invalid payload:', payload);
                return;
            }
            
            renderDashboard(payload);
        });
        
        socket.on('disconnect', () => {
            console.log('Disconnected from server');
        });
    </script>
</body>
</html>
        """

@app.route('/<path:path>')
def static_files(path):
    if os.path.exists(app.static_folder):
        return send_from_directory(app.static_folder, path)
    return "File not found", 404

@socketio.on('connect')
def handle_connect():
    socketio.emit('mqtt_status', {'connected': mqtt_connected})
    
    if latest_telemetry:
        socketio.emit('telemetry', latest_telemetry)
    
    socketio.emit('stats', stats)

@socketio.on('disconnect')
def handle_disconnect():
    pass

@socketio.on('request_stats')
def handle_stats_request():
    socketio.emit('stats', stats)

def main():
    print("Reactor Web Dashboard")
    print("=" * 60)
    
    init_mqtt()
    
    print(f"Server: http://localhost:5000")
    print(f"MQTT Broker: {MQTT_BROKER_HOST}:{MQTT_BROKER_PORT}")
    print()
    
    try:
        socketio.run(app, host='0.0.0.0', port=5000, debug=False)
    except KeyboardInterrupt:
        print("\nShutting down...")
        if mqtt_client:
            mqtt_client.loop_stop()
            mqtt_client.disconnect()

if __name__ == "__main__":
    main()

