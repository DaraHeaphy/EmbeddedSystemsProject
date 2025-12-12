@echo off
REM Start React Dashboard Backend

echo ============================================================
echo Starting Reactor Dashboard Backend
echo ============================================================
echo.

REM Check if dependencies are installed
python -c "import flask" 2>NUL
if errorlevel 1 (
    echo Installing Python dependencies...
    pip install -r requirements.txt
    echo.
)

echo Dashboard backend starting...
echo Make sure mqtt_broker.py is running!
echo.
echo Backend will be at: http://localhost:5000
echo.

cd ..
python server\web_dashboard.py

