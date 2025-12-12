#!/usr/bin/env python3
"""
Complete System Launcher
Starts: MQTT Broker -> ESP32 Simulator -> Web Dashboard
"""

import subprocess
import sys
import time
import signal
import os

processes = []

def signal_handler(sig, frame):
    print("\nShutting down all services...")
    for p in processes:
        try:
            p.terminate()
        except:
            pass
    sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)

def start_process(name, command):
    print(f"Starting {name}...")
    if sys.platform == "win32":
        p = subprocess.Popen(
            command,
            creationflags=subprocess.CREATE_NEW_CONSOLE
        )
    else:
        p = subprocess.Popen(command)
    processes.append(p)
    return p

def main():
    print("=" * 60)
    print("Reactor Telemetry System")
    print("=" * 60)
    print()
    
    start_process("MQTT Broker", [sys.executable, "mqtt_broker.py"])
    time.sleep(2)
    
    start_process("ESP32 Simulator", [sys.executable, "esp32_simulator.py"])
    time.sleep(1)
    
    start_process("Web Dashboard", [sys.executable, "web_dashboard.py"])
    time.sleep(2)
    
    print()
    print("=" * 60)
    print("All services started!")
    print("=" * 60)
    print()
    print("Dashboard: http://localhost:5000")
    print()
    print("Press Ctrl+C to stop all services")
    print()
    
    try:
        while True:
            time.sleep(1)
            for p in processes:
                if p.poll() is not None:
                    print(f"Process exited unexpectedly")
                    signal_handler(None, None)
    except KeyboardInterrupt:
        signal_handler(None, None)

if __name__ == "__main__":
    main()

