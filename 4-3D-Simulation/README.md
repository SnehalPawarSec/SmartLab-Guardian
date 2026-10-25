# 4 — 3D Simulation (Digital Twin)

A complete 3D simulation of the lab that runs **in a browser, offline, with nothing to
install**. It is used to demonstrate and explain the system when the real hardware is not
in the room — in a presentation, a viva, or a video.

## What is in this folder

| File | What it is |
|---|---|
| `START_SIMULATION.bat` | **Double-click this** — opens the simulation in Chrome or Edge |
| `SmartLab_3D_Digital_Twin.html` | The whole simulation in one file (about 11 MB) |
| `HOW_TO_RUN.txt` | The same instructions as plain text |

## How to run it

1. Double-click **`START_SIMULATION.bat`**.
2. Wait 5–10 seconds while it loads the 3D people (`Loading 3D people... 15 / 15`).
3. Click the page once, so the browser is allowed to play the buzzer sound.

No internet. No Python. No installation. Keep the `.bat` and the `.html` in the same folder.

> You can also just double-click `SmartLab_3D_Digital_Twin.html` directly. The `.bat` only
> exists to open it maximised in Chrome or Edge, which looks better for a demo.

## What to click — the demo, in order

Use the **Demo guide** panel on the left:

| # | Demo | What it shows |
|---|---|---|
| 1 | Camera detection | The laptop camera counts faces, lights switch ON / OFF |
| 2 | Full automatic demo | Door → lights ON → camera sees the student → timer → OFF |
| 3 | Door sensor | The door opens but nobody enters |
| 4 | Exam crowd alert | More than 5 people → CROWD ALERT + buzzer (Exam Mode) |
| 5 | Energy monitoring | Computers + lights + fans, and the cost in ₹ |
| 6 | Manual override | Staff force OFF / ON, then back to AUTO |

**Keys:** `Space` = pause / resume, `N` = next step, **Reset** = start again.

**Camera views:** Top, Left, Right, Front, Back, Laptop cam, Corridor, Follow, Entrance.

## If it runs slowly

Open **Manual controls** and untick **High quality (slower)**.

## Important — this folder is standalone

The simulation is **deliberately not connected** to anything:

- it does **not** talk to the ESP32
- it does **not** read or write Firebase
- it does **not** talk to the dashboard in `1-Dashboard-Website`

It is a self-contained model used for explaining the system. The real, live system is
folders 1, 2 and 3. Nothing you do in the simulation changes any real hardware or data.
