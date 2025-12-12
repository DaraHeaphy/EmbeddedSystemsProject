@echo off
REM Complete System Launcher

echo ============================================================
echo Reactor Telemetry System
echo ============================================================
echo.

REM Check if dependencies are installed
python -c "import paho.mqtt.client" 2>NUL
if errorlevel 1 (
    echo Installing dependencies...
    pip install -r requirements.txt
    echo.
)

echo Starting all services...
echo.
echo This will open 3 windows:
echo   1. MQTT Broker
echo   2. ESP32 Simulator
echo   3. Web Dashboard
echo.
echo Dashboard will be at: http://localhost:5000
echo.
echo Press any key to start...
pause >NUL

REM Start broker in new window
start "MQTT Broker" cmd /k "python mqtt_broker.py"
timeout /t 2 /nobreak >NUL

REM Start simulator in new window
start "ESP32 Simulator" cmd /k "python esp32_simulator.py"
timeout /t 1 /nobreak >NUL

REM Start dashboard in new window
start "Web Dashboard" cmd /k "python web_dashboard.py"
timeout /t 2 /nobreak >NUL

echo.
echo ============================================================
echo All services started!
echo ============================================================
echo.
echo Dashboard: http://localhost:5000
echo.
echo Close the individual windows to stop services.
echo.
pause

