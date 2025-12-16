@echo off
REM Quick launcher for MQTT testing scripts

echo.
echo ========================================
echo    MQTT Broker Testing Menu
echo ========================================
echo.
echo 1. Test Broker Connectivity
echo 2. Start Subscriber (Python Frontend)
echo 3. Start Publisher (Simulated ESP32)
echo 4. Open Testing Guide
echo 5. Exit
echo.
set /p choice="Enter your choice (1-5): "

if "%choice%"=="1" goto connectivity
if "%choice%"=="2" goto subscriber
if "%choice%"=="3" goto publisher
if "%choice%"=="4" goto guide
if "%choice%"=="5" goto end

:connectivity
echo.
echo Running connectivity test...
echo.
python test_connectivity.py
pause
goto end

:subscriber
echo.
echo Starting MQTT Subscriber (Python Frontend)...
echo Keep this window open to receive messages!
echo Press Ctrl+C to stop
echo.
python test_subscriber.py
pause
goto end

:publisher
echo.
echo Starting MQTT Publisher (Simulated ESP32)...
echo Make sure subscriber is running in another window!
echo Press Ctrl+C to stop
echo.
python test_publisher.py
pause
goto end

:guide
echo.
echo Opening testing guide...
start TESTING_GUIDE.md
goto end

:end
echo.
echo Goodbye!

