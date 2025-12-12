@echo off
REM Start custom MQTT broker

echo ============================================================
echo Starting Custom MQTT Broker
echo ============================================================
echo.

cd ..
python server\mqtt_broker.py

