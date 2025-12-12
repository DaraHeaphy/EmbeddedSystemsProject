@echo off
REM Quick start script for MQTT test client

echo ============================================================
echo Starting MQTT Test Client
echo ============================================================
echo.

REM Check if paho-mqtt is installed
python -c "import paho.mqtt.client" 2>NUL
if errorlevel 1 (
    echo Installing dependencies...
    pip install -r requirements.txt
    echo.
)

echo Make sure Mosquitto is running in another terminal!
echo Command: mosquitto -v
echo.
echo Press any key to start the test client...
pause >NUL

cd ..
python testing\mqtt_test_client.py

