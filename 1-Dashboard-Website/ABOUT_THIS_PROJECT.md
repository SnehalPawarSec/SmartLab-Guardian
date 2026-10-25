# SmartLab Guardian

AI-enabled smart computer-lab energy management and monitoring system.

An ESP32 board switches the lab lights and fan, a laptop camera counts how many people
are in the room, a small agent on every lab computer reports its usage and electricity,
and a web dashboard shows everything live — power, energy, cost, alerts and events.

> This file is the overview of the **whole project**. The project is split into four
> numbered folders, which all sit next to each other in the main folder.

---

## The project is in 4 folders

| Folder | Runs on | What it is |
|---|---|---|
| **1-Dashboard-Website** *(you are here)* | the lab laptop | The web dashboard — live power, energy, cost, PC list, alerts, events, analytics, controls. Also contains the laptop-camera people counter. |
| **2-ESP32-Code** | the ESP32 board | The firmware: 2 relays (fan + LED strip), door switch, buzzer, 5-page OLED, Firebase upload. Wiring diagram included. |
| **3-PC-Agent** | every lab computer | A small program you install on each lab PC. It sends computer name, logged-in user, CPU/RAM, active/idle time, power (W), energy and cost to the dashboard. |
| **4-3D-Simulation** | any browser, offline | A 3D simulation of the lab in a single HTML file, for presentations and vivas when the hardware is not in the room. Standalone — not connected to anything. |

Folders **1, 2 and 3** are the real, live system. They all talk to one **Firebase Realtime
Database**, so they do not need to be on the same computer — only on the internet.

Folder **4** is separate: it is a self-contained model for explaining the system, and it
touches no hardware and no database.

```
   ESP32  ──┐
 (relays,   │
  OLED,     │
  door)     │
            ├──►  Firebase Realtime Database  ──►  Dashboard website
 Lab PCs ───┤                                       (browser)
 (PC Agent) │
            │
 Laptop ────┘
 camera

 4-3D-Simulation  ──►  nothing. Runs alone in a browser, offline.
```

**Keep the folders together in the same parent folder** — `START_SMARTLAB.bat`
starts the PC agent by looking for `../3-PC-Agent`.

---

## Quick start (lab laptop, 5 minutes)

1. Install **Python 3** from <https://www.python.org/downloads/> — on the first screen
   **tick “Add python.exe to PATH”**.
2. Double-click **`START_SMARTLAB.bat`** in this folder.
   It opens the dashboard, the camera and the PC agent together.
3. The browser opens at <http://localhost:5080>. Log in:

| Username | Password | What they see |
|---|---|---|
| `staff` | `Staff@123` | Everything — all pages, controls, settings, PC actions |
| `hod` | `Hod@123` | Summary only — power, energy, cost, PCs online, lights/fan, alerts |

> Change these passwords before using the project for real — see `web/js/config.js`.

Then follow **`../2-ESP32-Code/WIRING_AND_SETUP.txt`** for the hardware, and copy
**`../3-PC-Agent`** to the other lab computers.

To show the project without any hardware, go to **`../4-3D-Simulation`** and double-click
`START_SIMULATION.bat`.

---

## What you need

**Hardware**
- ESP32 DevKit V1 board
- 2-channel 5 V relay module
- 0.96" I2C OLED display (SSD1306)
- 5 V fan, LED strip, 220 Ω resistor
- Door reed switch, buzzer
- 5 V power supply, jumper wires, breadboard

**Software**
- Arduino IDE (for the ESP32)
- Python 3 (for the dashboard, camera and PC agent)
- A free Firebase account

---

## First-time Firebase setup (once)

The project already points at a demo Firebase project. To use **your own**:

1. Create a project at <https://console.firebase.google.com>.
2. Add a **Realtime Database** and a **Web app**, and copy the config it gives you into
   `web/js/config.js`.
3. Put the same database URL in `../3-PC-Agent/config.json` and in
   `../2-ESP32-Code/esp32_smartlab/esp32_smartlab.ino`.
4. In the console open **Realtime Database → Rules**, paste the whole of
   `firebase/database.rules.json` and press **Publish**.
5. In **Authentication → Sign-in method**, enable **Email/Password**.

---

## Documentation

- `README.md` (this folder) — the dashboard and the camera
- `../2-ESP32-Code/README.md` — flashing the ESP32
- `../2-ESP32-Code/WIRING_AND_SETUP.txt` — full wiring, flashing, Firebase rules,
  laptop setup and expected results
- `../3-PC-Agent/README.md` — installing the agent on each lab computer
- `../4-3D-Simulation/README.md` — running the 3D simulation for a demo or viva
- `../HOW_TO_UPLOAD_TO_GITHUB.pdf` — step-by-step guide for putting this project on
  GitHub (no commands, all clicking)

---

## Troubleshooting

| Problem | Fix |
|---|---|
| `python is not recognised` | Reinstall Python and tick **Add python.exe to PATH**, then restart the computer. |
| Dashboard opens but everything is 0 | The ESP32 / PC agent is not sending. Check the internet and that the database URL is the same in folders 1, 2 and 3. |
| ESP32 keeps restarting when the fan starts | Power the relays from a separate 5 V adapter — see the wiring file, section A. |
| Camera window shows no boxes | Point the camera at people’s faces; lower `MIN_SCORE` in `camera_detector/camera_presence.py`. |
| ESP32 won’t upload | Hold the **BOOT** button while it says “Connecting…”. |
| PC agent window did not open | `3-PC-Agent` is not next to `1-Dashboard-Website`. Put the folders back in the same parent folder. |
| Simulation is slow or will not load | Untick **High quality (slower)** in its Manual controls. It needs Chrome or Edge. |

---

## License

MIT — see `LICENSE` in this folder.
