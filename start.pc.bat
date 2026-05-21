@echo off
cd /d "%~dp0"

echo ==============================
echo Remote Cockpit PC Start
echo ==============================

echo Start Diagnostics Backend + Control Backend + React Web...
python start_pc.py

echo ==============================
echo Stopped.
echo ==============================

pause
