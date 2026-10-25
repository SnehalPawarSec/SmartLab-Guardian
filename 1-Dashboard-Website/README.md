# 1 — Dashboard Website

The web dashboard for SmartLab Guardian, plus the laptop-camera people counter.
Both of these run on the **lab laptop**.

> **New here?** Read **[START_HERE.txt](START_HERE.txt)** first, or
> **[ABOUT_THIS_PROJECT.md](ABOUT_THIS_PROJECT.md)** for the whole project
> (all four folders, the hardware, and the Firebase setup).
>
> Want to show the project with no hardware and no setup? Use
> **../4-3D-Simulation** instead — it is a 3D simulation that runs offline in a browser.

## What is in this folder

| File / folder | What it is |
|---|---|
| `web/` | The dashboard itself — plain HTML, CSS and JavaScript, no build step |
| `web/js/config.js` | **Edit this**: Firebase settings and the two login accounts |
| `camera_detector/` | Laptop camera face detection (YuNet) → people count → Firebase |
| `firebase/database.rules.json` | Database security rules — paste into the Firebase console |
| `firebase.json`, `.firebaserc` | Settings for `firebase deploy` (optional online hosting) |
| `START_SMARTLAB.bat` | **Double-click this** — starts the dashboard, the camera and the PC agent together |
| `START_DASHBOARD_ONLY.bat` | Starts just the dashboard, nothing else |
| `START_HERE.txt` | Short plain-text summary of what to do first |
| `ABOUT_THIS_PROJECT.md` | Overview of the whole project (all three folders) |
| `LICENSE`, `.gitignore` | MIT license and the list of files Git should skip |

## Pages

| Page | Shows |
|---|---|
| `login.html` | Login (staff or HOD) |
| `dashboard.html` | Live overview — total power, energy today, cost, people, lights/fan |
| `pcs.html` | Every lab computer: user, CPU/RAM, active/idle, watts, kWh, cost |
| `alerts.html` | Crowd alerts, door alerts, offline alerts |
| `events.html` | Full history of what happened and when |
| `analytics.html` | Charts — usage and cost over time |
| `controls.html` | Staff controls — force lights/fan, exam mode, auto-off timer, settings |
| `data.html` | The raw JSON in the database (useful for the demo/viva) |

## How to run it

**On this computer (easiest):**

1. Install Python 3 — <https://www.python.org/downloads/> — tick **“Add python.exe to PATH”**.
2. Double-click **`START_SMARTLAB.bat`**.
   This starts the dashboard, the camera **and** the PC agent together.
3. The browser opens at <http://localhost:5080>.

If you only want the website and nothing else, double-click
`START_DASHBOARD_ONLY.bat` instead.

`START_SMARTLAB.bat` starts the PC agent by looking for `../3-PC-Agent`, so keep the
numbered folders together in the same parent folder.

**Online (optional), so anyone can open it from a phone:**

```
npm install -g firebase-tools
firebase login
firebase deploy
```

Run those from inside this folder.

## Logins

| Username | Password | Role |
|---|---|---|
| `staff` | `Staff@123` | Full control |
| `hod` | `Hod@123` | Summary view only |

The first successful login creates that account in Firebase Authentication.
Change the passwords in `web/js/config.js` before real use.

## The camera

`camera_detector/run_camera.bat` sets everything up by itself the first time
(it needs internet for 1–2 minutes) and then opens the camera window.

Keys in the camera window: **A** = auto, **O** = force off, **N** = force on, **Q** = quit.

If something that is not a person gets a box, raise `MIN_SCORE` (0.85 → 0.9) at the top of
`camera_detector/camera_presence.py`.
