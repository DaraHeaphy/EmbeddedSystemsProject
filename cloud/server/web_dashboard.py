#!/usr/bin/env python3

from flask import Flask, send_from_directory, request, jsonify
from flask_socketio import SocketIO
from flask_cors import CORS
import paho.mqtt.client as mqtt
import json
import threading
from datetime import datetime
import os
import socket

MQTT_BROKER_HOST = "alderaan.software-engineering.ie"
MQTT_BROKER_PORT = 1883

# client ids must be unique or the broker kicks old connections
MQTT_CLIENT_ID_EXACT = (os.getenv("MQTT_CLIENT_ID_EXACT") or "").strip()
if MQTT_CLIENT_ID_EXACT:
    MQTT_CLIENT_ID = MQTT_CLIENT_ID_EXACT
else:
    MQTT_CLIENT_ID_PREFIX = (os.getenv("MQTT_CLIENT_ID") or "web_dashboard").strip() or "web_dashboard"
    MQTT_CLIENT_ID = f"{MQTT_CLIENT_ID_PREFIX}_{socket.gethostname()}_{os.getpid()}"

MQTT_TOPICS = ["reactor/#", "students/#"]
MQTT_DEBUG = os.getenv("MQTT_DEBUG", "").lower() in ("1", "true", "yes", "on")

app = Flask(__name__, static_folder='dashboard/build')
CORS(app)
app.config['SECRET_KEY'] = 'reactor_secret_key'
socketio = SocketIO(app, cors_allowed_origins="*")

mqtt_client = None
mqtt_connected = False
latest_telemetry = {}
stats = {"messages_received": 0, "connection_time": None, "uptime": 0}


def on_mqtt_connect(client, userdata, flags, rc, properties=None):
    global mqtt_connected
    rc_int = int(rc) if not isinstance(rc, int) else rc
    if rc_int == 0:
        mqtt_connected = True
        stats["connection_time"] = datetime.now().isoformat()
        print(f"[MQTT] Connected client_id={MQTT_CLIENT_ID}")
        for topic in MQTT_TOPICS:
            client.subscribe(topic)
        socketio.emit('mqtt_status', {'connected': True, 'client_id': MQTT_CLIENT_ID, 'rc': 0})
    else:
        mqtt_connected = False
        print(f"[MQTT] Connection failed rc={rc_int}")
        socketio.emit('mqtt_status', {'connected': False, 'error': rc_int, 'client_id': MQTT_CLIENT_ID})


def on_mqtt_disconnect(client, userdata, rc, properties=None):
    global mqtt_connected
    mqtt_connected = False
    rc_int = int(rc) if not isinstance(rc, int) else rc
    print(f"[MQTT] Disconnected rc={rc_int}")
    socketio.emit('mqtt_status', {'connected': False, 'rc': rc_int, 'client_id': MQTT_CLIENT_ID})


def on_mqtt_log(client, userdata, level, buf):
    if MQTT_DEBUG:
        print(f"[MQTT][LOG] {buf}")


def on_mqtt_message(client, userdata, msg):
    global latest_telemetry, stats
    stats["messages_received"] += 1
    timestamp = datetime.now().isoformat()

    try:
        raw_payload = msg.payload.decode('utf-8')
        payload = json.loads(raw_payload)
        data = {'topic': msg.topic, 'payload': payload, 'timestamp': timestamp}

        topic_lower = msg.topic.lower()
        is_telemetry_topic = any(k in topic_lower for k in ("telemetry", "sensors", "stats"))
        is_telemetry_payload = isinstance(payload, dict) and any(
            k in payload for k in ("sample_id", "temp", "temp_c", "accel_mag", "power", "state")
        )

        if is_telemetry_topic or is_telemetry_payload:
            latest_telemetry = data
            socketio.emit('telemetry', data)
            print(f"[MQTT] Telemetry: {msg.topic}")
        elif "alerts" in msg.topic:
            socketio.emit('alert', data)
            print(f"[MQTT] Alert: {msg.topic}")
        else:
            socketio.emit('message', data)
            print(f"[MQTT] Message: {msg.topic}")

    except json.JSONDecodeError as e:
        print(f"JSON decode error: {e}")
    except Exception as e:
        print(f"Error processing message: {e}")


def init_mqtt():
    global mqtt_client
    mqtt_client = mqtt.Client(client_id=MQTT_CLIENT_ID)
    mqtt_client.on_connect = on_mqtt_connect
    mqtt_client.on_disconnect = on_mqtt_disconnect
    mqtt_client.on_message = on_mqtt_message
    mqtt_client.reconnect_delay_set(min_delay=1, max_delay=30)

    if MQTT_DEBUG:
        print(f"[MQTT] Debug enabled")
        mqtt_client.on_log = on_mqtt_log

    try:
        print(f"Connecting to MQTT broker: {MQTT_BROKER_HOST}:{MQTT_BROKER_PORT}")
        mqtt_client.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60)
        mqtt_client.loop_start()
        print("MQTT client started")
    except Exception as e:
        print(f"MQTT connection failed: {e}")


@app.route('/noises.mp3')
def audio():
    return send_from_directory('public', 'noises.mp3')


@app.route('/api/telemetry', methods=['POST'])
def receive_telemetry():
    global latest_telemetry, stats
    try:
        payload = request.get_json()
        if not payload:
            return jsonify({"error": "No JSON payload"}), 400

        stats["messages_received"] += 1
        timestamp = datetime.now().isoformat()
        data = {'topic': 'http/telemetry', 'payload': payload, 'timestamp': timestamp}

        latest_telemetry = data
        socketio.emit('telemetry', data)
        print(f"[HTTP] Telemetry: sample={payload.get('sample_id')} temp={payload.get('temp')} state={payload.get('state')}")

        return jsonify({"status": "ok", "sample_id": payload.get('sample_id')}), 200
    except Exception as e:
        print(f"[HTTP] Error: {e}")
        return jsonify({"error": str(e)}), 500


@app.route('/')
def index():
    if os.path.exists(os.path.join(app.static_folder, 'index.html')):
        return send_from_directory(app.static_folder, 'index.html')
    else:
        return EMBEDDED_DASHBOARD


@app.route('/<path:path>')
def static_files(path):
    if os.path.exists(app.static_folder):
        return send_from_directory(app.static_folder, path)
    return "File not found", 404


@socketio.on('connect')
def handle_connect():
    socketio.emit('mqtt_status', {'connected': mqtt_connected, 'client_id': MQTT_CLIENT_ID})
    if latest_telemetry:
        socketio.emit('telemetry', latest_telemetry)
    socketio.emit('stats', stats)


@socketio.on('disconnect')
def handle_disconnect():
    pass


@socketio.on('request_stats')
def handle_stats_request():
    socketio.emit('stats', stats)


@socketio.on('send_command')
def handle_send_command(data):
    if not mqtt_connected or not mqtt_client:
        socketio.emit('command_result', {'success': False, 'error': 'MQTT not connected'})
        return

    command = data.get('command')
    value = data.get('value')

    if not command:
        socketio.emit('command_result', {'success': False, 'error': 'No command specified'})
        return

    payload = {'command': command}
    if command == 'SET_POWER' and value is not None:
        payload['value'] = int(value)

    try:
        result = mqtt_client.publish('reactor/commands', json.dumps(payload), qos=1)
        if result.rc == 0:
            print(f"[CMD] Published: {payload}")
            socketio.emit('command_result', {'success': True, 'command': command})
        else:
            socketio.emit('command_result', {'success': False, 'error': f'Publish failed: {result.rc}'})
    except Exception as e:
        print(f"[CMD] Error: {e}")
        socketio.emit('command_result', {'success': False, 'error': str(e)})


EMBEDDED_DASHBOARD = """
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Reactor Telemetry</title>
    <script src="https://cdn.socket.io/4.5.4/socket.io.min.js"></script>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }

        body {
            font-family: 'Courier New', Consolas, monospace;
            background: #1a1a1a;
            color: #d4a574;
            display: flex;
            flex-direction: column;
            min-height: 100vh;
            background-image:
                repeating-linear-gradient(0deg, rgba(0,0,0,0.15) 0px, transparent 1px, transparent 2px, rgba(0,0,0,0.15) 3px),
                repeating-linear-gradient(90deg, rgba(0,0,0,0.15) 0px, transparent 1px, transparent 2px, rgba(0,0,0,0.15) 3px);
            background-size: 4px 4px;
        }

        header {
            background: #0a0a0a;
            padding: 1rem 2rem;
            border-bottom: 4px solid #4a4a4a;
            box-shadow: 0 4px 0 #2a2a2a, inset 0 -2px 10px rgba(0,0,0,0.5);
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
            text-shadow: 2px 2px 4px rgba(0,0,0,0.8);
        }
        h1::before { content: '///'; margin-right: 1rem; color: #8b7355; }
        h1::after { content: '///'; margin-left: 1rem; color: #8b7355; }

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
            box-shadow: inset 2px 2px 4px rgba(0,0,0,0.8);
        }
        .connected { color: #8ba574; border-color: #5a6a4a; }
        .disconnected { color: #a57474; border-color: #6a4a4a; }

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
            box-shadow: inset 2px 2px 8px rgba(0,0,0,0.8), 2px 2px 0 #2a2a2a;
        }
        .metric-card::after {
            content: '';
            position: absolute;
            top: 0; left: 0; right: 0; bottom: 0;
            background: repeating-linear-gradient(0deg, transparent, transparent 2px, rgba(0,0,0,0.1) 2px, rgba(0,0,0,0.1) 4px);
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
        .metric-label::before { content: '> '; color: #6a5a4a; }

        .metric-value {
            font-size: 2rem;
            font-weight: 700;
            font-family: 'Courier New', monospace;
            color: #d4a574;
            line-height: 1;
            letter-spacing: 2px;
            text-shadow: 1px 1px 2px rgba(0,0,0,0.8);
        }
        .metric-unit { font-size: 1rem; color: #8b7355; margin-left: 0.5rem; }
        .metric-sublabel { font-size: 0.65rem; color: #5a5a5a; margin-top: 0.75rem; text-transform: uppercase; letter-spacing: 1px; }

        .state-card { grid-column: 1 / -1; }
        .state-indicators {
            display: flex;
            justify-content: center;
            align-items: center;
            gap: 3rem;
            margin: 0.5rem auto;
            max-width: 800px;
        }
        .state-indicator { display: flex; flex-direction: column; align-items: center; gap: 0.75rem; }

        .state-light {
            width: 70px;
            height: 70px;
            border-radius: 50%;
            border: 4px solid #2a2a2a;
            box-shadow: inset 3px 3px 10px rgba(0,0,0,0.9), 2px 2px 0 rgba(0,0,0,0.5);
            position: relative;
            transition: all 0.3s ease;
        }
        .state-light::after {
            content: '';
            position: absolute;
            top: 20%; left: 20%;
            width: 30%; height: 30%;
            border-radius: 50%;
            background: rgba(255,255,255,0.1);
        }
        .state-light.active { border-width: 6px; }
        .state-light.normal { background: #1a1a1a; border-color: #2a2a2a; }
        .state-light.normal.active { background: #8ba574; border-color: #5a6a4a; box-shadow: 0 0 20px rgba(139,165,116,0.6), inset 2px 2px 8px rgba(0,0,0,0.5); }
        .state-light.warning { background: #1a1a1a; border-color: #2a2a2a; }
        .state-light.warning.active { background: #d4a574; border-color: #8b7355; box-shadow: 0 0 20px rgba(212,165,116,0.6), inset 2px 2px 8px rgba(0,0,0,0.5); }
        .state-light.scram { background: #1a1a1a; border-color: #2a2a2a; }
        .state-light.scram.active { background: #a57474; border-color: #6a4a4a; box-shadow: 0 0 20px rgba(165,116,116,0.6), inset 2px 2px 8px rgba(0,0,0,0.5); }

        .state-text { font-size: 1rem; font-weight: 700; letter-spacing: 2px; text-transform: uppercase; opacity: 0.3; transition: all 0.3s ease; }
        .state-text.active { opacity: 1; text-shadow: 2px 2px 4px rgba(0,0,0,0.8); }
        .state-text.normal { color: #8ba574; }
        .state-text.warning { color: #d4a574; }
        .state-text.scram { color: #a57474; }

        .dial-container {
            position: relative;
            width: 220px;
            height: 220px;
            margin: 1rem auto 0.5rem;
            background: radial-gradient(circle, #1a1a1a 0%, #0a0a0a 100%);
            border-radius: 50%;
            border: 4px solid #2a2a2a;
            box-shadow: inset 0 0 20px rgba(0,0,0,0.9), inset 0 0 8px rgba(0,0,0,0.5), 0 2px 0 #1a1a1a;
        }
        .dial-svg { width: 100%; height: 100%; }
        .dial-face { fill: none; stroke: #3a3a3a; stroke-width: 2; }
        .dial-tick { stroke: #5a5a5a; stroke-width: 2; stroke-linecap: round; }
        .dial-tick-major { stroke: #8b7355; stroke-width: 3; }
        .dial-number { fill: #8b7355; font-family: 'Courier New', monospace; font-size: 12px; font-weight: 700; text-anchor: middle; }
        .dial-needle { stroke: #d4a574; stroke-width: 3; stroke-linecap: round; filter: drop-shadow(2px 2px 2px rgba(0,0,0,0.8)); transition: transform 0.3s ease-out; transform-origin: center; }
        .dial-center { fill: #2a2a2a; stroke: #5a5a5a; stroke-width: 2; }

        .thermometer-container { width: 60%; height: 220px; margin: 1rem auto 0.5rem; position: relative; display: flex; justify-content: center; align-items: center; }
        .thermometer-svg { width: 100%; height: 100%; }
        .thermometer-tube { fill: #0a0a0a; stroke: #3a3a3a; stroke-width: 2; }
        .thermometer-bulb { fill: #1a1a1a; stroke: #3a3a3a; stroke-width: 2; }
        .thermometer-fill { fill: #d4a574; transition: height 0.3s ease-out; }
        .thermometer-tick { stroke: #5a5a5a; stroke-width: 1; }
        .thermometer-tick-major { stroke: #8b7355; stroke-width: 2; }
        .thermometer-number { fill: #8b7355; font-family: 'Courier New', monospace; font-size: 10px; font-weight: 700; text-anchor: end; }
        .thermometer-outline { fill: none; stroke: #3a3a3a; stroke-width: 3; }

        .power-bar-container { width: 140px; height: 220px; margin: 1rem auto 0.5rem; position: relative; display: flex; justify-content: center; align-items: center; }
        .power-bar-svg { width: 100%; height: 100%; }
        .power-bar-bg { fill: #0a0a0a; stroke: #3a3a3a; stroke-width: 2; }
        .power-bar-segment { fill: #2a2a2a; stroke: #1a1a1a; stroke-width: 1; }
        .power-bar-segment.active { fill: #d4a574; }
        .power-bar-tick { stroke: #5a5a5a; stroke-width: 1; }
        .power-bar-number { fill: #8b7355; font-family: 'Courier New', monospace; font-size: 10px; font-weight: 700; text-anchor: start; }
        .power-bar-outline { fill: none; stroke: #3a3a3a; stroke-width: 2; }

        @media (max-width: 1200px) {
            .telemetry-grid { grid-template-columns: 1fr; }
            .state-indicators { gap: 2rem; }
            .state-light { width: 80px; height: 80px; }
        }
        @media (max-width: 768px) {
            .metric-value { font-size: 2rem; }
            .state-indicators { gap: 1.5rem; }
            .state-light { width: 60px; height: 60px; }
            .state-text { font-size: 1rem; }
        }

        .operator-btn {
            padding: 0.5rem 1rem;
            background: linear-gradient(180deg, #8b0000 0%, #5a0000 100%);
            border: 2px solid #aa0000;
            color: #ffffff;
            font-family: 'Courier New', monospace;
            font-size: 0.75rem;
            font-weight: 700;
            text-transform: uppercase;
            letter-spacing: 2px;
            cursor: pointer;
            box-shadow: 0 2px 0 #2a0000;
            transition: all 0.1s ease;
        }
        .operator-btn:hover { background: linear-gradient(180deg, #aa0000 0%, #7a0000 100%); box-shadow: 0 2px 0 #2a0000, 0 0 15px rgba(139,0,0,0.5); }
        .operator-btn:active { transform: translateY(1px); box-shadow: 0 1px 0 #2a0000; }

        .modal-overlay {
            display: none;
            position: fixed;
            top: 0; left: 0; right: 0; bottom: 0;
            background: rgba(0,0,0,0.85);
            z-index: 1000;
            justify-content: center;
            align-items: center;
            backdrop-filter: blur(4px);
        }
        .modal-overlay.active { display: flex; }

        .control-panel {
            background: #0a0a0a;
            border: 4px solid #8b0000;
            width: 90%;
            max-width: 500px;
            position: relative;
            box-shadow: 0 0 60px rgba(139,0,0,0.5), 0 0 100px rgba(139,0,0,0.2), inset 0 0 30px rgba(0,0,0,0.8);
            animation: modal-appear 0.3s ease-out;
        }
        @keyframes modal-appear {
            from { opacity: 0; transform: scale(0.9) translateY(-20px); }
            to { opacity: 1; transform: scale(1) translateY(0); }
        }
        .control-panel::before, .control-panel::after {
            content: '';
            position: absolute;
            left: 0; right: 0;
            height: 8px;
            background: repeating-linear-gradient(90deg, #8b0000 0px, #8b0000 20px, #1a1a1a 20px, #1a1a1a 40px);
        }
        .control-panel::before { top: 0; }
        .control-panel::after { bottom: 0; }

        .panel-header {
            background: linear-gradient(180deg, #1a0000 0%, #0a0000 100%);
            padding: 1.5rem;
            text-align: center;
            border-bottom: 3px solid #8b0000;
            position: relative;
        }
        .panel-header::before, .panel-header::after {
            content: '// CLASSIFIED //';
            position: absolute;
            top: 0.5rem;
            font-size: 0.6rem;
            color: #8b0000;
            letter-spacing: 2px;
        }
        .panel-header::before { left: 1rem; }
        .panel-header::after { right: 1rem; }

        .panel-title { font-size: 1.1rem; font-weight: 700; color: #ff4444; text-transform: uppercase; letter-spacing: 4px; text-shadow: 0 0 10px rgba(255,68,68,0.5); margin-bottom: 0.5rem; }
        .panel-subtitle { font-size: 0.7rem; color: #8b0000; letter-spacing: 3px; text-transform: uppercase; }

        .panel-warning {
            background: repeating-linear-gradient(45deg, #1a1a00 0px, #1a1a00 10px, #0a0a00 10px, #0a0a00 20px);
            padding: 0.5rem;
            text-align: center;
            border-bottom: 2px solid #4a4a00;
        }
        .warning-text { color: #d4d400; font-size: 0.65rem; font-weight: 700; letter-spacing: 2px; text-transform: uppercase; animation: warning-blink 2s ease-in-out infinite; }
        @keyframes warning-blink { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }

        .panel-body {
            padding: 2rem;
            background: repeating-linear-gradient(0deg, transparent 0px, transparent 2px, rgba(139,0,0,0.03) 2px, rgba(139,0,0,0.03) 4px);
        }
        .control-group { margin-bottom: 1.5rem; }
        .control-label { display: block; font-size: 0.7rem; color: #a57474; text-transform: uppercase; letter-spacing: 2px; margin-bottom: 0.75rem; font-weight: 700; }
        .control-label::before { content: '>'; margin-right: 0.5rem; color: #8b0000; }

        .control-select {
            width: 100%;
            padding: 1rem;
            background: #0f0f0f;
            border: 2px solid #4a2a2a;
            color: #d4a574;
            font-family: 'Courier New', monospace;
            font-size: 1rem;
            font-weight: 700;
            letter-spacing: 1px;
            cursor: pointer;
            appearance: none;
            background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12'%3E%3Cpath fill='%238b0000' d='M6 8L1 3h10z'/%3E%3C/svg%3E");
            background-repeat: no-repeat;
            background-position: right 1rem center;
            box-shadow: inset 2px 2px 8px rgba(0,0,0,0.8);
        }
        .control-select:focus { outline: none; border-color: #8b0000; box-shadow: inset 2px 2px 8px rgba(0,0,0,0.8), 0 0 10px rgba(139,0,0,0.3); }
        .control-select option { background: #0a0a0a; color: #d4a574; padding: 0.5rem; }

        .control-input {
            width: 100%;
            padding: 1rem;
            background: #0f0f0f;
            border: 2px solid #4a2a2a;
            color: #d4a574;
            font-family: 'Courier New', monospace;
            font-size: 1.2rem;
            font-weight: 700;
            letter-spacing: 2px;
            text-align: center;
            box-shadow: inset 2px 2px 8px rgba(0,0,0,0.8);
        }
        .control-input:focus { outline: none; border-color: #8b0000; box-shadow: inset 2px 2px 8px rgba(0,0,0,0.8), 0 0 10px rgba(139,0,0,0.3); }
        .control-input::placeholder { color: #4a3a3a; }

        .value-group { display: none; }
        .value-group.visible { display: block; }

        .execute-btn {
            width: 100%;
            padding: 1.25rem 2rem;
            background: linear-gradient(180deg, #8b0000 0%, #5a0000 50%, #3a0000 100%);
            border: 3px solid #aa0000;
            color: #ffffff;
            font-family: 'Courier New', monospace;
            font-size: 1.1rem;
            font-weight: 700;
            text-transform: uppercase;
            letter-spacing: 4px;
            cursor: pointer;
            position: relative;
            overflow: hidden;
            box-shadow: 0 4px 0 #2a0000, 0 6px 10px rgba(0,0,0,0.5), inset 0 1px 0 rgba(255,255,255,0.1);
            transition: all 0.1s ease;
        }
        .execute-btn:hover { background: linear-gradient(180deg, #aa0000 0%, #7a0000 50%, #5a0000 100%); box-shadow: 0 4px 0 #2a0000, 0 6px 15px rgba(139,0,0,0.4), inset 0 1px 0 rgba(255,255,255,0.2); }
        .execute-btn:active { transform: translateY(2px); box-shadow: 0 2px 0 #2a0000, 0 3px 5px rgba(0,0,0,0.5), inset 0 1px 0 rgba(255,255,255,0.1); }
        .execute-btn::before { content: ''; position: absolute; top: 0; left: -100%; width: 100%; height: 100%; background: linear-gradient(90deg, transparent, rgba(255,255,255,0.1), transparent); transition: left 0.5s ease; }
        .execute-btn:hover::before { left: 100%; }
        .execute-btn.scram-active { animation: scram-pulse 0.5s ease-in-out infinite; }
        @keyframes scram-pulse {
            0%, 100% { box-shadow: 0 4px 0 #2a0000, 0 0 20px rgba(255,0,0,0.5); }
            50% { box-shadow: 0 4px 0 #2a0000, 0 0 40px rgba(255,0,0,0.8); }
        }

        .panel-footer { background: #050505; padding: 1rem; border-top: 2px solid #3a1a1a; display: flex; justify-content: space-between; align-items: center; }
        .auth-badge { font-size: 0.6rem; color: #5a3a3a; letter-spacing: 1px; text-transform: uppercase; }
        .status-indicator { display: flex; align-items: center; gap: 0.5rem; }
        .status-dot { width: 8px; height: 8px; border-radius: 50%; background: #3a3a3a; }
        .status-dot.ready { background: #4a8b4a; box-shadow: 0 0 8px rgba(74,139,74,0.5); }
        .status-dot.busy { background: #d4a574; animation: status-pulse 1s ease-in-out infinite; }
        @keyframes status-pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.4; } }
        .status-text-small { font-size: 0.65rem; color: #5a5a5a; text-transform: uppercase; letter-spacing: 1px; }

        .command-log { margin-top: 1rem; padding: 0.75rem; background: #050505; border: 1px solid #2a1a1a; font-size: 0.7rem; color: #6a5a5a; font-family: 'Courier New', monospace; max-height: 60px; overflow-y: auto; }
        .command-log .success { color: #8ba574; }
        .command-log .error { color: #a57474; }
    </style>
</head>
<body>
    <audio id="ambient-audio" loop autoplay>
        <source src="/noises.mp3" type="audio/mpeg">
    </audio>

    <header>
        <h1>Reactor Telemetry</h1>
        <div class="status">
            <button id="operator-btn" class="operator-btn">Operator Panel</button>
            <span id="mqtt-status" class="status-badge disconnected">DISCONNECTED</span>
            <span style="color: #a8b2d1;">Messages: <span id="msg-count">0</span></span>
        </div>
    </header>

    <main>
        <div id="content"></div>
    </main>

    <div id="modal-overlay" class="modal-overlay">
        <div class="control-panel">
            <div class="panel-header">
                <div class="panel-title">Reactor Command Interface</div>
                <div class="panel-subtitle">Authorized Personnel Only</div>
            </div>
            <div class="panel-warning">
                <span class="warning-text">Caution: Commands Are Transmitted Directly To Reactor Core Systems</span>
            </div>
            <div class="panel-body">
                <div class="control-group">
                    <label class="control-label">Select Command</label>
                    <select id="command-select" class="control-select">
                        <option value="">-- SELECT COMMAND --</option>
                        <option value="SCRAM">SCRAM - Emergency Shutdown</option>
                        <option value="RESET_NORMAL">RESET_NORMAL - Return To Normal Operation</option>
                        <option value="SET_POWER">SET_POWER - Adjust Power Level</option>
                    </select>
                </div>
                <div id="value-group" class="control-group value-group">
                    <label class="control-label">Power Level (0-100)</label>
                    <input type="number" id="power-value" class="control-input" min="0" max="100" value="50" placeholder="0-100">
                </div>
                <button id="execute-btn" class="execute-btn" disabled>Execute Command</button>
                <div id="command-log" class="command-log">> System ready. Awaiting operator input...</div>
            </div>
            <div class="panel-footer">
                <span class="auth-badge">Clearance Level: Operator</span>
                <div class="status-indicator">
                    <span id="cmd-status-dot" class="status-dot ready"></span>
                    <span id="cmd-status-text" class="status-text-small">Ready</span>
                </div>
            </div>
        </div>
    </div>

    <script>
        const audio = document.getElementById('ambient-audio');
        audio.volume = 0.3;
        document.addEventListener('click', () => {
            if (audio.paused) audio.play().catch(() => {});
        }, { once: true });
    </script>
    <script>
        const socket = io();
        let msgCount = 0;

        function renderDashboard(payload = null) {
            const currentState = payload?.state || 'NORMAL';
            const temp = payload?.temp ?? 0;
            const accel = payload?.accel_mag ?? 9.81;
            const power = payload?.power ?? 0;
            const sampleId = payload?.sample_id ?? 0;

            const minAccel = 0, maxAccel = 25;
            const clampedAccel = Math.max(minAccel, Math.min(maxAccel, accel));
            const angle = ((clampedAccel - minAccel) / (maxAccel - minAccel)) * 270 - 135;

            const minTemp = 0, maxTemp = 100;
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
                        <div class="metric-value">${temp.toFixed(2)}<span class="metric-unit">°C</span></div>
                        <div class="thermometer-container">
                            <svg class="thermometer-svg" viewBox="0 0 100 160" preserveAspectRatio="xMidYMid meet">
                                <rect class="thermometer-tube" x="35" y="20" width="30" height="100" rx="15" />
                                ${Array.from({length: 11}, (_, i) => {
                                    const value = 100 - (i * 10);
                                    const y = 20 + (i * 100 / 10);
                                    const isMajor = i % 2 === 0;
                                    return `<line class="${isMajor ? 'thermometer-tick-major' : 'thermometer-tick'}" x1="35" y1="${y}" x2="${35 - (isMajor ? 8 : 5)}" y2="${y}" />
                                            ${isMajor ? `<text class="thermometer-number" x="24" y="${y + 3}">${value}</text>` : ''}`;
                                }).join('')}
                                <clipPath id="tube-clip"><rect x="35" y="20" width="30" height="100" rx="15" /></clipPath>
                                <rect class="thermometer-fill" x="35" y="${120 - tempPercent}" width="30" height="${tempPercent}" clip-path="url(#tube-clip)" />
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
                        <div class="metric-value">${accel.toFixed(3)}<span class="metric-unit">m/s²</span></div>
                        <div class="dial-container">
                            <svg class="dial-svg" viewBox="0 0 200 200">
                                <defs><radialGradient id="glassGrad" cx="30%" cy="30%"><stop offset="0%" style="stop-color:rgba(255,255,255,0.1)" /><stop offset="100%" style="stop-color:transparent" /></radialGradient></defs>
                                <circle class="dial-face" cx="100" cy="100" r="85" />
                                ${Array.from({length: 26}, (_, i) => {
                                    const a = (i * 270 / 25) - 135;
                                    const isMajor = i % 5 === 0;
                                    const len = isMajor ? 15 : 10;
                                    const rad = (a * Math.PI) / 180;
                                    const r = 70;
                                    return `<line class="${isMajor ? 'dial-tick-major' : 'dial-tick'}" x1="${100 + Math.cos(rad) * r}" y1="${100 + Math.sin(rad) * r}" x2="${100 + Math.cos(rad) * (r - len)}" y2="${100 + Math.sin(rad) * (r - len)}" />`;
                                }).join('')}
                                ${[0, 5, 10, 15, 20, 25].map((val, i) => {
                                    const a = (i * 270 / 5) - 135;
                                    const rad = (a * Math.PI) / 180;
                                    return `<text class="dial-number" x="${100 + Math.cos(rad) * 50}" y="${100 + Math.sin(rad) * 50 + 4}">${val}</text>`;
                                }).join('')}
                                <line class="dial-needle" x1="100" y1="100" x2="100" y2="30" style="transform: rotate(${angle}deg)" />
                                <circle class="dial-center" cx="100" cy="100" r="6" />
                                <circle cx="100" cy="100" r="85" fill="url(#glassGrad)" />
                            </svg>
                        </div>
                        <div class="metric-sublabel">Magnitude</div>
                    </div>

                    <div class="metric-card">
                        <div class="metric-label">Power Output</div>
                        <div class="metric-value">${power}<span class="metric-unit">%</span></div>
                        <div class="power-bar-container">
                            <svg class="power-bar-svg" viewBox="0 0 120 160" preserveAspectRatio="xMidYMid meet">
                                <rect class="power-bar-bg" x="30" y="10" width="60" height="130" />
                                ${Array.from({length: 10}, (_, i) => {
                                    const y = 10 + (i * 13);
                                    const segmentPower = 100 - (i * 10);
                                    return `<rect class="power-bar-segment ${power >= segmentPower ? 'active' : ''}" x="32" y="${y + 1}" width="56" height="11" />`;
                                }).join('')}
                                ${[100, 75, 50, 25, 0].map((val, i) => `<line class="power-bar-tick" x1="90" y1="${10 + (i * 32.5)}" x2="95" y2="${10 + (i * 32.5)}" /><text class="power-bar-number" x="98" y="${10 + (i * 32.5) + 4}">${val}</text>`).join('')}
                                <rect class="power-bar-outline" x="30" y="10" width="60" height="130" />
                            </svg>
                        </div>
                        <div class="metric-sublabel">Current Level</div>
                    </div>
                </div>
            `;
        }

        renderDashboard();

        socket.on('connect', () => console.log('Connected to server'));

        socket.on('mqtt_status', (status) => {
            const badge = document.getElementById('mqtt-status');
            badge.title = Object.entries(status).map(([k, v]) => `${k}=${v}`).join(' ');
            badge.textContent = status.connected ? 'CONNECTED' : 'DISCONNECTED';
            badge.className = 'status-badge ' + (status.connected ? 'connected' : 'disconnected');
        });

        socket.on('telemetry', (data) => {
            msgCount++;
            document.getElementById('msg-count').textContent = msgCount;
            if (data.payload && typeof data.payload === 'object') renderDashboard(data.payload);
        });

        socket.on('disconnect', (reason) => console.log('Disconnected:', reason));

        // operator panel
        const modalOverlay = document.getElementById('modal-overlay');
        const operatorBtn = document.getElementById('operator-btn');
        const commandSelect = document.getElementById('command-select');
        const valueGroup = document.getElementById('value-group');
        const powerValue = document.getElementById('power-value');
        const executeBtn = document.getElementById('execute-btn');
        const commandLog = document.getElementById('command-log');
        const cmdStatusDot = document.getElementById('cmd-status-dot');
        const cmdStatusText = document.getElementById('cmd-status-text');

        operatorBtn.addEventListener('click', () => modalOverlay.classList.add('active'));
        modalOverlay.addEventListener('click', (e) => { if (e.target === modalOverlay) modalOverlay.classList.remove('active'); });
        document.addEventListener('keydown', (e) => { if (e.key === 'Escape') modalOverlay.classList.remove('active'); });

        function logCommand(msg, type = 'info') {
            const ts = new Date().toLocaleTimeString();
            commandLog.innerHTML += `<div class="${type}">[${ts}] ${msg}</div>`;
            commandLog.scrollTop = commandLog.scrollHeight;
        }

        function setStatus(status, text) {
            cmdStatusDot.className = 'status-dot ' + status;
            cmdStatusText.textContent = text;
        }

        commandSelect.addEventListener('change', () => {
            const cmd = commandSelect.value;
            valueGroup.classList.toggle('visible', cmd === 'SET_POWER');
            executeBtn.disabled = !cmd;
            if (cmd === 'SCRAM') {
                executeBtn.textContent = '!! EXECUTE SCRAM !!';
                executeBtn.classList.add('scram-active');
            } else {
                executeBtn.textContent = 'Execute Command';
                executeBtn.classList.remove('scram-active');
            }
        });

        executeBtn.addEventListener('click', () => {
            const cmd = commandSelect.value;
            if (!cmd) return;
            const payload = { command: cmd };
            if (cmd === 'SET_POWER') payload.value = parseInt(powerValue.value) || 50;
            setStatus('busy', 'Transmitting...');
            executeBtn.disabled = true;
            logCommand(`Transmitting: ${cmd}${payload.value !== undefined ? ' (value=' + payload.value + ')' : ''}...`);
            socket.emit('send_command', payload);
        });

        socket.on('command_result', (result) => {
            if (result.success) {
                logCommand(`Command ${result.command} executed successfully`, 'success');
                setStatus('ready', 'Ready');
            } else {
                logCommand(`ERROR: ${result.error}`, 'error');
                setStatus('ready', 'Error');
            }
            executeBtn.disabled = !commandSelect.value;
        });
    </script>
</body>
</html>
"""


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
