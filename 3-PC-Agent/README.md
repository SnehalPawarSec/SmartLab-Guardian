# 3 — PC Agent

Install this on **every lab computer**. It quietly reports that computer to the dashboard:

- computer name and the logged-in user
- CPU and RAM use
- active time vs idle time
- power in watts, energy in kWh and the cost today
- the active app (can be switched off)

It can also be told by staff from the dashboard to **lock** the computer.

## What is in this folder

| File | What it is |
|---|---|
| `SmartLabAgent.exe` | Ready-to-run agent — **Python is not needed** |
| `pc_agent.py` | The same agent as Python source |
| `install_agent.bat` | **Double-click this** — sets up, sends a test, offers auto-start |
| `run_agent.bat` | Run it now in a window and watch the log |
| `uninstall_agent.bat` | Remove the auto-start entry |
| `add_startup.ps1` | Helper used by `install_agent.bat` |
| `config.json` | **Edit this** — database URL and options |
| `build_exe.bat` | Rebuild `SmartLabAgent.exe` from the Python source |
| `requirements.txt` | Python packages (`psutil`, `requests`) |

## How to install it on a lab computer

1. Copy this whole **3-PC-Agent** folder onto that computer (USB stick is fine).
2. Open `config.json` in Notepad and check `databaseURL` is your Firebase database.
3. Double-click **`install_agent.bat`**.
4. It sends one test report. If that works it asks
   *“Start the agent automatically when Windows starts?”* — answer **Y**.
5. The computer now appears on the dashboard’s **PC Monitoring** page within ~10 seconds.

`SmartLabAgent.exe` is already in the folder, so a lab computer **does not need Python**.
If you deleted the .exe, install Python 3 first and tick “Add python.exe to PATH”.

## Settings in `config.json`

| Setting | Meaning |
|---|---|
| `databaseURL` | Your Firebase Realtime Database URL |
| `pcName` | Leave `""` to use the real Windows computer name |
| `intervalSec` | How often it reports, in seconds (default 10) |
| `idleAfterSec` | No keyboard/mouse for this long counts as idle (default 60) |
| `powerIdleW` / `powerMaxW` | Watts used when idle / at full load. `0` = auto-estimate |
| `reportActiveApp` | `false` to stop sending which app is in front (privacy) |
| `allowRemoteLock` | `true` lets staff lock this computer from the dashboard |
| `allowRemoteShutdown` | `true` lets staff shut it down — off by default |

## To remove it

Run `uninstall_agent.bat`, then delete the folder.
