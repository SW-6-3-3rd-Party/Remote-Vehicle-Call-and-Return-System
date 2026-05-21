@echo off
cd /d C:\Users\USER\Desktop\remote-cockpit

echo ==============================
echo Remote Cockpit PC Web Start
echo ==============================

npm run dev -- --host 0.0.0.0
pause