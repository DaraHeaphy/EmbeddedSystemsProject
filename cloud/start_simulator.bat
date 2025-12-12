@echo off
REM Start ESP32 simulator

echo ============================================================
echo Starting ESP32 Reactor Simulator
echo ============================================================
echo.

REM Check if paho-mqtt is installed
python -c "import paho.mqtt.client" 2>NUL
if errorlevel 1 (
    echo Installing dependencies...
    pip install -r requirements.txt
    echo.
)

echo Simulating ESP32 MQTT publisher...
echo Make sure mqtt_broker.py is running in another terminal!
echo.
echo Press any key to start the simulator...
pause >NUL

python esp32_simulator.py

