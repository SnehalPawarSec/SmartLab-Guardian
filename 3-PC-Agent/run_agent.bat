@echo off
REM SmartLab Guardian - run the PC agent in this window (Ctrl+C to stop)
cd /d "%~dp0"
if exist "SmartLabAgent.exe" (
  SmartLabAgent.exe %*
) else (
  python pc_agent.py %*
)
pause
