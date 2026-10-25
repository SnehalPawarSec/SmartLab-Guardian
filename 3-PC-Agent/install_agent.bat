@echo off
REM SmartLab Guardian - install the PC agent on this computer
cd /d "%~dp0"
echo ================================================
echo   SmartLab Guardian - PC agent setup
echo ================================================
echo.

if exist "SmartLabAgent.exe" (
  echo Found SmartLabAgent.exe - Python is not needed.
  goto test
)

where python >nul 2>nul
if errorlevel 1 (
  echo Python is not installed on this PC.
  echo Either install Python 3 from https://www.python.org/downloads/  ^(tick "Add python.exe to PATH"^)
  echo or copy SmartLabAgent.exe into this folder and run this file again.
  pause
  exit /b 1
)
echo Installing Python packages ^(psutil, requests^)...
python -m pip install --disable-pip-version-check -q -r requirements.txt
if errorlevel 1 (
  echo pip install failed - check the internet connection.
  pause
  exit /b 1
)

:test
echo.
echo Sending one test report to Firebase...
if exist "SmartLabAgent.exe" ( SmartLabAgent.exe --once ) else ( python pc_agent.py --once )
if errorlevel 1 (
  echo Test failed - see the message above.
  pause
  exit /b 1
)
echo.
choice /m "Start the agent automatically when Windows starts"
if errorlevel 2 goto done
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0add_startup.ps1"
echo To remove it later, run uninstall_agent.bat

:done
echo.
echo Done. Double-click run_agent.bat to start it now and watch the log.
pause
