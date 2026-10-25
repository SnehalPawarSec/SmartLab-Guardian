# 2 — ESP32 Code

The firmware for the ESP32 board: it switches the lab fan and LED strip, reads the door
switch, sounds the crowd buzzer, shows 5 pages on the OLED and sends everything to Firebase.

## What is in this folder

| File / folder | What it is |
|---|---|
| `esp32_smartlab/esp32_smartlab.ino` | The sketch — open this one in the Arduino IDE |
| `WIRING_AND_SETUP.txt` | **Read this first** — full wiring, flashing and setup guide |

## Wiring (short version — full details in `WIRING_AND_SETUP.txt`)

| Part | ESP32 pin |
|---|---|
| OLED SDA | G21 |
| OLED SCL | G22 |
| Relay IN1 — fan | G25 |
| Relay IN2 — LED strip | G33 |
| Door reed switch | G27 |
| Buzzer (+) | G18 |

All grounds must be connected together.

## How to flash it

1. Install the **Arduino IDE** — <https://www.arduino.cc/en/software>.
2. **File → Preferences → Additional boards manager URLs**, paste:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. **Tools → Board → Boards Manager**, search `esp32` by Espressif, **Install**.
4. **Tools → Manage Libraries**, install **Adafruit SSD1306** (choose *Install All*)
   and **ArduinoJson** 7.x.
5. Open `esp32_smartlab/esp32_smartlab.ino`.
6. Near the top, fill in your Wi-Fi and your database URL:

   ```cpp
   const char* WIFI_NAME     = "YOUR_WIFI_NAME";
   const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
   const char* FIREBASE_URL  = "https://your-project-default-rtdb.firebasedatabase.app";
   ```

   The Wi-Fi must be **2.4 GHz and have working internet** — a phone hotspot with mobile
   data is fine.
7. **Tools → Board → DOIT ESP32 DEVKIT V1**, pick the COM port, press **Upload**.
   If it sticks on “Connecting…”, hold the **BOOT** button on the board.

## The 5 OLED pages (3 seconds each)

1. **LAB STATUS** — mode, people counted by the camera, camera, door, auto-off timer
2. **LIGHT & FAN** — on/off, volts, mA, watts, Wh today
3. **COMPUTERS** — online, active/idle, PC watts, PC kWh today
4. **TOTAL LAB** — total watts, kWh today, cost (₹), PCs vs lights+fan
5. **NETWORK** — Wi-Fi, signal, cloud, IP, uptime

More than 5 people → **CROWD ALERT** screen, a dashboard alert, and 3 buzzer beeps every
5 seconds while **exam mode** is on.

## Settings you may want to change

Near the top of the sketch:

| Setting | Meaning |
|---|---|
| `EMPTY_TIMEOUT_SEC` | Seconds of an empty room before the lights/fan switch off (20 for the demo, 600 for a real lab) |
| `MAX_PEOPLE` | Crowd-alert limit (the dashboard setting overrides it) |
| `PIR_INSTALLED` | `true` if you fitted an HC-SR501 motion sensor on G13 |
| `METER_INSTALLED` | `true` if you wired the real voltage/current measuring, `false` to estimate |
| `OLED_INSTALLED` | `false` if you are running without a display |
