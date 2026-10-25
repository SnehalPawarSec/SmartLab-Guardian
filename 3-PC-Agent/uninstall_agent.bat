@echo off
REM SmartLab Guardian - remove the agent from Windows startup and stop it
del "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\SmartLab Agent.lnk" 2>nul
taskkill /f /im SmartLabAgent.exe >nul 2>nul
wmic process where "name='pythonw.exe' and commandline like '%%pc_agent.py%%'" call terminate >nul 2>nul
echo SmartLab agent removed from startup and stopped.
pause
