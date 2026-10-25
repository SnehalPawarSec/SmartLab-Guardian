@echo off
title SmartLab Camera (person detection)
cd /d "%~dp0"
where python >nul 2>nul || (
  echo Python is not installed.
  echo Install it from https://www.python.org/downloads/  and TICK "Add python.exe to PATH".
  pause & exit /b 1
)
if not exist .venv\Scripts\python.exe (
  echo First run: setting up camera software ^(needs internet, 1-2 minutes^)...
  python -m venv .venv
)
.venv\Scripts\python -m pip install --disable-pip-version-check -q -r requirements.txt
.venv\Scripts\python camera_presence.py
pause
