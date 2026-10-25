"""
SmartLab Guardian - PC Monitoring Agent
=======================================
Runs on each lab PC / laptop and reports to Firebase every few seconds:

    name, CPU %, RAM %, disk %, ACTIVE / IDLE (keyboard + mouse), idle time,
    last activity, active app, uptime, battery, IP, estimated power (W),
    energy used today (kWh), active / idle time today, and a server timestamp.

It also listens for commands from the dashboard (show a message, lock the PC,
and shut down - shut down only if allowed in config.json) and raises an alert
when the PC stays idle too long.

Setup:   pip install -r requirements.txt        (or double-click install_agent.bat)
Run:     python pc_agent.py                     (or double-click run_agent.bat)
Test:    python pc_agent.py --once              (send one report and exit)
Stop:    Ctrl + C  (the PC is marked OFFLINE)

All settings are in config.json next to this file.
"""

import argparse
import ctypes
import getpass
import json
import logging
import os
import platform
import re
import socket
import sys
import threading
import time
from datetime import date, datetime
from logging.handlers import RotatingFileHandler

import psutil
import requests

VERSION = "2.1"

# Folder of the script, or of the .exe when built with PyInstaller
BASE_DIR = os.path.dirname(sys.executable if getattr(sys, "frozen", False) else os.path.abspath(__file__))
CONFIG_FILE = os.path.join(BASE_DIR, "config.json")
STATE_FILE = os.path.join(BASE_DIR, "agent_state.json")
LOG_FILE = os.path.join(BASE_DIR, "agent.log")

DEFAULT_CONFIG = {
    "databaseURL": "https://smartlab-guardian-1f880-default-rtdb.asia-southeast1.firebasedatabase.app",
    "pcName": "",                # empty = use the computer name
    "intervalSec": 10,           # seconds between reports (demo 10, real lab 30)
    "idleAfterSec": 60,          # no keyboard/mouse for this long -> IDLE
    "powerIdleW": 0,             # 0 = automatic (laptop 10 W, desktop 55 W)
    "powerMaxW": 0,              # 0 = automatic (laptop 45 W, desktop 130 W)
    "reportActiveApp": True,     # send the name of the program in front (e.g. chrome.exe)
    "allowRemoteLock": True,
    "allowRemoteShutdown": False,
}

IS_WINDOWS = platform.system() == "Windows"
log = logging.getLogger("agent")


# ----------------------------------------------------------------------------- setup
def setup_logging() -> None:
    log.setLevel(logging.INFO)
    fmt = logging.Formatter("%(asctime)s  %(message)s", "%H:%M:%S")
    fh = RotatingFileHandler(LOG_FILE, maxBytes=500_000, backupCount=1, encoding="utf-8")
    fh.setFormatter(logging.Formatter("%(asctime)s  %(message)s"))
    log.addHandler(fh)
    if sys.stdout is not None:            # pythonw / --noconsole has no console
        sh = logging.StreamHandler(sys.stdout)
        sh.setFormatter(fmt)
        log.addHandler(sh)


def load_config() -> dict:
    cfg = dict(DEFAULT_CONFIG)
    if os.path.exists(CONFIG_FILE):
        with open(CONFIG_FILE, encoding="utf-8") as f:
            cfg.update(json.load(f))
    else:
        with open(CONFIG_FILE, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2)
    cfg["databaseURL"] = cfg["databaseURL"].rstrip("/")
    return cfg


def single_instance() -> bool:
    """Only one agent per PC (startup copy + manual copy would double-count energy)."""
    if not IS_WINDOWS:
        return True
    ctypes.windll.kernel32.CreateMutexW(None, False, "Global\\SmartLabGuardianAgent")
    return ctypes.windll.kernel32.GetLastError() != 183    # ERROR_ALREADY_EXISTS


def safe_key(name: str) -> str:
    """Firebase keys cannot contain . $ # [ ] /"""
    return re.sub(r"[.$#\[\]/\s]", "_", name.strip()) or "PC"


# ----------------------------------------------------------------------------- Firebase REST
class Firebase:
    def __init__(self, base: str):
        self.base = base
        self.s = requests.Session()

    def _url(self, path: str) -> str:
        return f"{self.base}/smartlab/{path}.json"

    def get(self, path: str):
        r = self.s.get(self._url(path), timeout=10)
        r.raise_for_status()
        return r.json()

    def patch(self, path: str, data: dict) -> None:
        self.s.patch(self._url(path), json=data, timeout=10).raise_for_status()

    def post(self, path: str, data: dict) -> None:
        self.s.post(self._url(path), json=data, timeout=10).raise_for_status()


TS = {".sv": "timestamp"}      # Firebase fills in its own server time


# ----------------------------------------------------------------------------- Windows helpers
def seconds_since_input() -> float:
    """Seconds since the last keyboard/mouse input. -1 if unknown (not Windows)."""
    if not IS_WINDOWS:
        return -1

    class LASTINPUTINFO(ctypes.Structure):
        _fields_ = [("cbSize", ctypes.c_uint), ("dwTime", ctypes.c_uint)]

    info = LASTINPUTINFO()
    info.cbSize = ctypes.sizeof(LASTINPUTINFO)
    if not ctypes.windll.user32.GetLastInputInfo(ctypes.byref(info)):
        return -1
    millis = (ctypes.windll.kernel32.GetTickCount() - info.dwTime) & 0xFFFFFFFF
    return millis / 1000.0


def foreground_app() -> str:
    """Name of the program in front, e.g. 'chrome.exe'. Window titles are not sent (privacy)."""
    if not IS_WINDOWS:
        return ""
    try:
        hwnd = ctypes.windll.user32.GetForegroundWindow()
        pid = ctypes.c_ulong()
        ctypes.windll.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        return psutil.Process(pid.value).name() if pid.value else ""
    except (psutil.Error, OSError):
        return ""


def show_message(text: str) -> None:
    """Pop-up on the PC screen (does not block the agent)."""
    if IS_WINDOWS:
        # MB_OK | MB_ICONINFORMATION | MB_SYSTEMMODAL | MB_SETFOREGROUND
        threading.Thread(target=lambda: ctypes.windll.user32.MessageBoxW(0, text, "SmartLab Guardian", 0x40 | 0x1000 | 0x10000),
                         daemon=True).start()


def local_ip() -> str:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("8.8.8.8", 80))        # no packet is sent; just picks the outgoing interface
            return s.getsockname()[0]
    except OSError:
        return ""


# ----------------------------------------------------------------------------- system info
def current_user() -> str:
    """Windows user name of the person logged in (e.g. 'student01')."""
    try:
        return os.environ.get("USERNAME") or getpass.getuser()
    except Exception:
        return ""


def cpu_model() -> str:
    if IS_WINDOWS:
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as k:
                return winreg.QueryValueEx(k, "ProcessorNameString")[0].strip()
        except OSError:
            pass
    return platform.processor() or ""


# ----------------------------------------------------------------------------- agent
class Agent:
    def __init__(self, cfg: dict, name_override: str = ""):
        self.cfg = cfg
        self.name = name_override or cfg["pcName"] or socket.gethostname()
        self.key = safe_key(self.name)
        self.fb = Firebase(cfg["databaseURL"])
        self.battery_present = psutil.sensors_battery() is not None
        self.type = "LAPTOP" if self.battery_present else "DESKTOP"
        self.p_idle = cfg["powerIdleW"] or (10 if self.battery_present else 55)
        self.p_max = cfg["powerMaxW"] or (45 if self.battery_present else 130)
        self.ip = local_ip()
        self.cpu_model = cpu_model()
        self.ram_gb = round(psutil.virtual_memory().total / 1024 ** 3, 1)
        self.settings = {"pcIdleMin": 10}
        self.settings_at = 0.0
        self.idle_alerted = False
        self.last_tick = time.time()
        self.state = self._load_state()
        psutil.cpu_percent(None)          # first call primes the counter

    # ---- persistent daily counters (survive restarts)
    def _load_state(self) -> dict:
        s = {}
        try:
            with open(STATE_FILE, encoding="utf-8") as f:
                s = json.load(f)
        except (OSError, ValueError):
            pass
        if s.get("date") != date.today().isoformat():
            s.update(date=date.today().isoformat(), energyKwh=0.0, activeSec=0.0, idleSec=0.0)
        s.setdefault("lastCommandId", None)
        return s

    def _save_state(self) -> None:
        try:
            with open(STATE_FILE, "w", encoding="utf-8") as f:
                json.dump(self.state, f)
        except OSError:
            pass

    # ---- one measurement
    def collect(self) -> dict:
        now = time.time()
        dt = min(now - self.last_tick, 3 * self.cfg["intervalSec"])   # ignore sleep/hibernate gaps
        self.last_tick = now
        if self.state["date"] != date.today().isoformat():            # new day -> reset counters
            self.state.update(date=date.today().isoformat(), energyKwh=0.0, activeSec=0.0, idleSec=0.0)

        cpu = psutil.cpu_percent(None)
        ram = psutil.virtual_memory().percent
        try:
            disk = psutil.disk_usage(os.environ.get("SystemDrive", "C:") + "\\" if IS_WINDOWS else "/").percent
        except OSError:
            disk = None

        idle_sec = seconds_since_input()
        if idle_sec < 0:                          # not Windows: guess from CPU use
            status, idle_sec = ("ACTIVE" if cpu > 10 else "IDLE"), 0
        else:
            status = "IDLE" if idle_sec >= self.cfg["idleAfterSec"] else "ACTIVE"

        # Estimated power: idle watts + share of the extra watts by CPU load
        power = self.p_idle + (self.p_max - self.p_idle) * cpu / 100.0
        self.state["energyKwh"] += power * dt / 3_600_000
        self.state["activeSec" if status == "ACTIVE" else "idleSec"] += dt

        batt = psutil.sensors_battery()
        data = {
            "name": self.name,
            "hostname": socket.gethostname(),
            "user": current_user(),
            "cpuModel": self.cpu_model,
            "ramGB": self.ram_gb,
            "cpu": round(cpu, 1),
            "ram": round(ram, 1),
            "disk": round(disk, 1) if disk is not None else None,
            "status": status,
            "idleSec": int(idle_sec),
            "lastActivity": datetime.fromtimestamp(now - idle_sec).strftime("%I:%M:%S %p"),
            "activeApp": foreground_app() if self.cfg["reportActiveApp"] else "",
            "uptimeSec": int(now - psutil.boot_time()),
            "battery": {"percent": int(batt.percent), "plugged": bool(batt.power_plugged)} if batt else None,
            "ip": self.ip,
            "os": f"{platform.system()} {platform.release()}",
            "type": self.type,
            "powerW": round(power, 1),
            "energyTodayKwh": round(self.state["energyKwh"], 4),
            "activeSecToday": int(self.state["activeSec"]),
            "idleSecToday": int(self.state["idleSec"]),
            "interval": self.cfg["intervalSec"],
            "allowLock": bool(self.cfg["allowRemoteLock"]),
            "allowShutdown": bool(self.cfg["allowRemoteShutdown"]),
            "agentVersion": VERSION,
            "simulated": False,
            "lastSeen": TS,
        }
        return data

    # ---- send + side jobs
    def report(self) -> dict:
        data = self.collect()
        self.fb.patch(f"computers/{self.key}", data)
        self.fb.patch(f"pcDaily/{self.state['date']}/{self.key}", {
            "name": self.name, "kwh": data["energyTodayKwh"],
            "activeSec": data["activeSecToday"], "idleSec": data["idleSecToday"], "t": TS})
        self._save_state()
        self._idle_alert(data)
        self._commands()
        return data

    def _refresh_settings(self) -> None:
        if time.time() - self.settings_at < 300:
            return
        self.settings_at = time.time()
        try:
            self.settings.update(self.fb.get("settings") or {})
        except requests.RequestException:
            pass

    def _idle_alert(self, data: dict) -> None:
        self._refresh_settings()
        limit = float(self.settings.get("pcIdleMin", 10)) * 60
        if data["status"] == "IDLE" and data["idleSec"] >= limit and not self.idle_alerted:
            self.idle_alerted = True
            self.fb.post("alerts", {"t": TS, "type": "PC_IDLE", "severity": "info", "source": self.name,
                                    "msg": f"{self.name} has been idle for {data['idleSec'] // 60} min but is still ON ({data['powerW']:.0f} W)."})
        elif data["status"] == "ACTIVE":
            self.idle_alerted = False

    def _commands(self) -> None:
        cmd = self.fb.get(f"pcCommands/{self.key}")
        if not cmd or not cmd.get("id") or cmd.get("id") == self.state.get("lastCommandId"):
            return
        self.state["lastCommandId"] = cmd["id"]
        self._save_state()
        if cmd.get("doneId") == cmd["id"]:
            return
        action, text = cmd.get("action"), (cmd.get("text") or "Message from lab staff")[:200]
        result = "ok"
        if action == "message":
            show_message(text)
        elif action == "lock":
            if self.cfg["allowRemoteLock"] and IS_WINDOWS:
                ctypes.windll.user32.LockWorkStation()
            else:
                result = "lock not allowed on this PC"
        elif action == "shutdown":
            if self.cfg["allowRemoteShutdown"] and IS_WINDOWS:
                show_message("This PC will shut down in 60 seconds (requested by lab staff). Save your work.")
                os.system('shutdown /s /t 60 /c "SmartLab Guardian: shutdown requested by lab staff"')
            else:
                result = "shutdown not allowed on this PC (allowRemoteShutdown is false)"
        else:
            result = f"unknown action {action}"
        log.info("command %s from %s -> %s", action, cmd.get("by"), result)
        self.fb.patch(f"pcCommands/{self.key}", {"doneId": cmd["id"], "doneAt": TS, "result": result})
        self.fb.post("events", {"t": TS, "type": "PC_COMMAND", "source": self.name, "msg": f"{self.name}: {action} -> {result}"})

    def mark_existing_command_done(self) -> None:
        """On start, don't replay an old command (e.g. yesterday's shutdown)."""
        try:
            cmd = self.fb.get(f"pcCommands/{self.key}")
            if cmd and cmd.get("id"):
                self.state["lastCommandId"] = cmd["id"]
        except requests.RequestException:
            pass

    def event(self, msg: str) -> None:
        try:
            self.fb.post("events", {"t": TS, "type": "PC", "source": self.name, "msg": msg})
        except requests.RequestException:
            pass

    def go_offline(self) -> None:
        try:
            self.fb.patch(f"computers/{self.key}", {"status": "OFFLINE", "powerW": 0, "lastSeen": TS})
            self.event(f"{self.name} agent stopped (PC offline)")
        except requests.RequestException:
            pass
        self._save_state()


def main() -> None:
    ap = argparse.ArgumentParser(description="SmartLab Guardian PC agent")
    ap.add_argument("--once", action="store_true", help="send one report and exit (test)")
    ap.add_argument("--name", default="", help="override the PC name, e.g. PC-01")
    args = ap.parse_args()

    setup_logging()
    if not args.once and not single_instance():     # --once is a quick test, allowed next to a running agent
        log.info("Another SmartLab agent is already running on this PC. Exiting.")
        return
    cfg = load_config()
    agent = Agent(cfg, args.name)
    log.info("SmartLab agent v%s  |  %s (%s)  ->  %s/smartlab/computers/%s",
             VERSION, agent.name, agent.type, cfg["databaseURL"], agent.key)

    if args.once:
        time.sleep(1)
        d = agent.report()
        log.info("sent: %s CPU %s%% RAM %s%% %s W", d["status"], d["cpu"], d["ram"], d["powerW"])
        return

    agent.mark_existing_command_done()
    agent.event(f"{agent.name} agent started (PC online, {agent.type.lower()})")
    fails = 0
    try:
        while True:
            start = time.time()
            try:
                d = agent.report()
                fails = 0
                log.info("%-6s CPU %5.1f%%  RAM %5.1f%%  %5.1f W  today %.3f kWh  app %s",
                         d["status"], d["cpu"], d["ram"], d["powerW"], d["energyTodayKwh"], d["activeApp"] or "-")
            except requests.RequestException as e:
                fails += 1
                log.info("network error (%d): %s", fails, str(e)[:150])
            # back off a little when the network is down
            wait = cfg["intervalSec"] * (1 if fails < 3 else 3)
            time.sleep(max(1.0, wait - (time.time() - start)))
    except KeyboardInterrupt:
        log.info("Stopping - marking this PC OFFLINE")
        agent.go_offline()


if __name__ == "__main__":
    main()
