@echo off
title SmartLab Guardian - 3D Simulation
REM Opens the SmartLab Guardian 3D simulation (works offline, nothing to install).
REM Uses Google Chrome if installed, otherwise Microsoft Edge, otherwise the default browser.
cd /d "%~dp0"
set "FILE=%~dp0SmartLab_3D_Digital_Twin.html"

if not exist "%FILE%" (
  echo SmartLab_3D_Digital_Twin.html was not found next to this .bat file.
  echo Keep both files in the same folder.
  pause
  exit /b 1
)

set "CHROME="
if exist "%ProgramFiles%\Google\Chrome\Application\chrome.exe" set "CHROME=%ProgramFiles%\Google\Chrome\Application\chrome.exe"
if exist "%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe" set "CHROME=%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe"
if exist "%LocalAppData%\Google\Chrome\Application\chrome.exe" set "CHROME=%LocalAppData%\Google\Chrome\Application\chrome.exe"
if defined CHROME (
  start "" "%CHROME%" --new-window --start-maximized "%FILE%"
  goto :done
)

set "EDGE="
if exist "%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe" set "EDGE=%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"
if exist "%ProgramFiles%\Microsoft\Edge\Application\msedge.exe" set "EDGE=%ProgramFiles%\Microsoft\Edge\Application\msedge.exe"
if defined EDGE (
  start "" "%EDGE%" --new-window --start-maximized "%FILE%"
  goto :done
)

start "" "%FILE%"

:done
echo SmartLab Guardian 3D simulation is opening in the browser...
timeout /t 3 >nul
