@echo off
REM Builds SmartLabAgent.exe so lab PCs don't need Python.
REM Copy SmartLabAgent.exe + config.json + install_agent.bat + run_agent.bat to the lab PC.
cd /d "%~dp0"
python -m pip install -q pyinstaller psutil requests
python -m PyInstaller --onefile --name SmartLabAgent --clean pc_agent.py
if errorlevel 1 ( echo Build failed. & pause & exit /b 1 )
copy /y dist\SmartLabAgent.exe . >nul
rmdir /s /q build 2>nul
del SmartLabAgent.spec 2>nul
echo.
echo Built SmartLabAgent.exe
pause
