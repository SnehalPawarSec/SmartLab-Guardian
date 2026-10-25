@echo off
REM ============================================================
REM  SmartLab Guardian - START EVERYTHING on the LAB LAPTOP
REM    1) dashboard web server  http://localhost:5080
REM    2) camera detector       (laptop camera -> people count -> ESP32)
REM    3) PC agent              (this laptop reports its usage + electricity)
REM ============================================================
cd /d "%~dp0"
where python >nul 2>nul || ( echo Python 3 is required: https://www.python.org/downloads/  ^(tick "Add python.exe to PATH"^) & pause & exit /b 1 )
echo Checking Python packages...
python -m pip install --disable-pip-version-check -q psutil requests

start "SmartLab Dashboard (keep this window open)" /min cmd /c "cd /d "%~dp0web" && python -m http.server 5080 --bind 127.0.0.1"
start "SmartLab Camera" cmd /k "cd /d "%~dp0camera_detector" && run_camera.bat"

if exist "%~dp0..\3-PC-Agent\pc_agent.py" (
  start "SmartLab PC Agent" cmd /k "cd /d "%~dp0..\3-PC-Agent" && python pc_agent.py"
) else (
  echo.
  echo NOTE: the folder 3-PC-Agent was not found next to this one,
  echo       so this laptop will not report itself to the dashboard.
  echo       Keep 1-Dashboard-Website and 3-PC-Agent in the same parent folder.
  echo.
)

timeout /t 3 >nul
start "" http://localhost:5080/
echo.
echo Dashboard: http://localhost:5080
echo   staff / Staff@123  = full control      hod / Hod@123  = summary view
echo.
echo Close the opened windows to stop everything.
