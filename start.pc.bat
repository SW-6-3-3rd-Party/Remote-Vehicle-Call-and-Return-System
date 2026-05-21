@echo off
cd /d C:\Users\USER\Desktop\remote-cockpit

echo ==============================
echo Remote Cockpit PC Start
echo ==============================

echo [1/2] Start PC Control Backend...
start "PC Control Backend" cmd /k "cd /d C:\Users\USER\Desktop\remote-cockpit && node pc_control_backend.cjs"

timeout /t 2 /nobreak > nul

echo [2/2] Start React Web...
start "React Remote Cockpit" cmd /k "cd /d C:\Users\USER\Desktop\remote-cockpit && npm run dev -- --host 0.0.0.0"

echo ==============================
echo Done. Two terminals are opened.
echo ==============================

pause