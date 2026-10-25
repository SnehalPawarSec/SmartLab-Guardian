@echo off
REM SmartLab Guardian - start ONLY the dashboard website on this computer.
REM (Use ..\START_SMARTLAB.bat instead if you also want the camera and the PC agent.)
cd /d "%~dp0"
where python >nul 2>nul || ( echo Python 3 is required: https://www.python.org/downloads/  ^(tick "Add python.exe to PATH"^) & pause & exit /b 1 )
start "SmartLab Dashboard (keep this window open)" /min cmd /c "cd /d "%~dp0web" && python -m http.server 5080 --bind 127.0.0.1"
timeout /t 2 >nul
start "" http://localhost:5080/
echo Dashboard: http://localhost:5080   (staff / Staff@123,  hod / Hod@123)
echo Close the minimised "SmartLab Dashboard" window to stop it.
pause
