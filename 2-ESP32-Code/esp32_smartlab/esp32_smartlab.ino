/*
  =====================================================================
   SmartLab Guardian - ESP32 firmware (final DC demo version)
   AI-Enabled Smart Computer Lab Energy Management & Monitoring (IoT)
  =====================================================================

  What this program does
  ----------------------
   1. PRESENCE: laptop camera (camera_detector, face detection) + door switch (G27).
      (Optional HC-SR501 PIR on G13, off by default: PIR_INSTALLED false.)
      The camera script writes commands/cameraPresent + cameraPeople to Firebase; the ESP32 reads them.
   2. AUTO mode : someone present -> LIGHT + FAN ON, lab empty for EMPTY_TIMEOUT_SEC -> OFF.
      MANUAL    : light and fan follow the dashboard (Controls page) or the camera window keys.
   3. OUTPUTS   : relay IN1 (G25) -> K1 -> 5V fan,  relay IN2 (G33) -> K2 -> LED strip.
   4. ENERGY    : lights + fan (5V DC, estimated from ratings, or measured with resistors on G34/G35)
                  + every lab computer running the PC agent (read from Firebase /computers)
                  = TOTAL lab power, energy today and cost. The OLED and the dashboard show the same numbers.
   5. CROWD     : more than MAX_PEOPLE faces on the camera -> alert on the dashboard, and the buzzer (G18)
                  beeps while EXAM MODE is ON (switch on the dashboard / Controls page).
   6. OLED      : 5 pages in sequence (lab status, light & fan, computers, total, network).
   7. ALERTS    : entry after college hours, crowd, high total power.

  Libraries (Arduino IDE -> Tools -> Manage Libraries):
   - "Adafruit SSD1306" by Adafruit (click "Install All")
   - "ArduinoJson" by Benoit Blanchon (version 7.x)
  Board: Tools -> Board -> esp32 -> "DOIT ESP32 DEVKIT V1"

  ONLY EDIT THE "YOUR SETTINGS" SECTION BELOW.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

// =====================================================================
//                         YOUR SETTINGS
// =====================================================================

// ---- Wi-Fi (2.4 GHz, MUST have internet: phone hotspot with mobile data, or laptop Mobile Hotspot) ----
const char* WIFI_NAME     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";   // "" for an open network without a login page

// ---- Firebase Realtime Database (no slash at the end) ----
const char* FIREBASE_URL  = "https://smartlab-guardian-1f880-default-rtdb.asia-southeast1.firebasedatabase.app";
const char* FIREBASE_AUTH = "";

// ---- Connected parts ----
#define PIR_INSTALLED     false   // HC-SR501 on G13 (removed from the demo: set true if you fit one)
#define METER_INSTALLED   false   // true = 1k+1k divider on G35 + 220 ohm under the LED on G34 (measured)
                                  // false = light + fan power ESTIMATED from their ratings
#define OLED_INSTALLED    true

// ---- Timing ----
unsigned long EMPTY_TIMEOUT_SEC = 20;       // demo 20 s, real lab 600 s (dashboard can change it)
const unsigned long CAMERA_STALE_SEC = 15;  // camera script silent this long -> camera ignored
const unsigned long OLED_PAGE_SEC = 3;      // each OLED page is shown this long

// ---- PIR accuracy ----
const unsigned long PIR_WARMUP_SEC      = 60;    // HC-SR501 gives random pulses for ~60 s after power-on
const unsigned long PIR_CONFIRM_MS      = 400;   // OUT must stay HIGH this long to count as motion
const unsigned long PIR_RELAY_IGNORE_MS = 2500;  // ignore PIR right after a relay clicks (supply noise)

// ---- Crowd alert (camera count). The dashboard setting "maxPeople" overrides this ----
int MAX_PEOPLE = 5;

// ---- Light + fan ratings (used when METER_INSTALLED = false) ----
const float FAN_RATED_AMPS = 0.12;   // fan sticker: DC 5V 0.12A
const float LED_EST_AMPS   = 0.015;  // LED strip through 220 ohm: about 15 mA
const float SUPPLY_VOLTS   = 5.0;

// ---- Resistor meter (used when METER_INSTALLED = true) ----
const float LED_SENSE_OHMS = 220.0;
const float DIVIDER_RATIO  = 2.0;

// ---- Relay / buzzer type ----
#define RELAY_ACTIVE_LOW   true      // blue 5V relay boards switch ON when IN = LOW
// Buzzer type and door direction are set from the dashboard (Controls -> Hardware Setup & Test),
// so no re-flashing is needed. These are only the start-up defaults:
#define BUZZER_DEFAULT     "ACTIVE"   // "ACTIVE" (beeps on HIGH), "ACTIVE_LOW" (3-pin module), "PASSIVE" (only clicks)
#define DOOR_INVERT_DEFAULT false     // true if the door shows OPEN when it is closed

// =====================================================================
//                              PIN MAP
// =====================================================================
const int PIN_RELAY_FAN   = 25;  // relay IN1 -> K1 -> fan        (fan - -> K1 NO, K1 COM -> GND)
const int PIN_RELAY_LIGHT = 33;  // relay IN2 -> K2 -> LED strip  (LED - -> K2 NO, K2 COM -> GND)
const int PIN_PIR         = 13;  // HC-SR501 OUT (VCC 5V, GND)
const int PIN_DOOR        = 27;  // door reed switch, other wire -> GND
const int PIN_BUZZER      = 18;  // buzzer +, buzzer - -> GND
const int PIN_LED_SENSE   = 34;  // only with METER_INSTALLED
const int PIN_VOLT_SENSE  = 35;  // only with METER_INSTALLED
const int PIN_LED_CLOUD   = 2;   // ESP32 board LED: ON = Firebase OK
// OLED: SDA = G21, SCL = G22

// =====================================================================
//                         GLOBAL STATE
// =====================================================================
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
WiFiClientSecure tls;
HTTPClient http;
Preferences prefs;

// Commands (dashboard / camera window)
String cmdMode  = "AUTO";
bool   cmdLight = false, cmdFan = false;
bool   examMode = true;         // dashboard "Exam Mode": ON = crowd buzzer enabled, OFF = buzzer silent
String buzzerTestId = "";       // dashboard "Test buzzer" button
String buzzerType = BUZZER_DEFAULT;   // from dashboard: ACTIVE / ACTIVE_LOW / PASSIVE
bool   doorInvert = DOOR_INVERT_DEFAULT;
bool   doorRaw = false;         // G27 pin level right now (for the Hardware Test panel)
bool   pirRaw = false;          // PIR OUT pin right now (for testing)

// Presence
bool camera = false, cameraFresh = false, cameraReported = false;
int  cameraPeople = 0;
long long cameraLastSeenMs = 0;
bool pir = false;
unsigned long pirHighSince = 0;
bool doorOpen = false, lastDoorOpen = false;
bool occupied = false, crowd = false;
int  peopleCount = 0;
unsigned long lastActivityMs = 0;
bool everActivity = false;
long emptyTimerLeft = 0;
unsigned long crowdSince = 0;

// Outputs
bool lightOn = false, fanOn = false;
unsigned long lastRelayChangeMs = 0;

// Light + fan (DC) energy
float vVolt = 0, vAmp = 0, vWatt = 0, ledAmp = 0, fanAmp = 0;
double dcKwhToday = 0;          // survives reboots (saved in flash every minute)
int   todayKey = 0;             // yyyymmdd of dcKwhToday
unsigned long lastMeterMs = 0;
bool meterOk = true;

// Computers (from the PC agents in Firebase)
int   pcOnline = 0, pcActive = 0, pcIdle = 0, pcTotal = 0;
float pcPowerW = 0;
double pcKwhToday = 0;
bool  pcDataOk = false;

// Totals (same numbers on OLED and dashboard)
float  totalW = 0;
double totalKwhToday = 0, costToday = 0;
float  tariff = 8.0, highPowerW = 2000;
int    labOpenHour = 8, labCloseHour = 18;

// Connectivity / timers
bool cloudOk = false, syncNow = false;
unsigned long tSensors = 0, tMeter = 0, tOled = 0, tSync = 0, tPcs = 0, tSettings = 0, tLog = 0, tSave = 0, tWifi = 0;
int oledPage = 0;
unsigned long oledPageAt = 0;

// Alerts (same type at most once every 5 minutes)
enum { AL_AFTER_HOURS = 0, AL_CROWD = 1, AL_HIGH_POWER = 2, AL_COUNT = 3 };
unsigned long lastAlertMs[AL_COUNT] = {0, 0, 0};

// =====================================================================
//                              HELPERS
// =====================================================================
void setRelayPin(int pin, bool on) {
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(pin, level ? HIGH : LOW);
}

void setBuzzer(bool on) {
  static bool toneOn = false;
  if (buzzerType == "PASSIVE") {                // passive buzzer needs a 2.7 kHz tone, not a DC level
    if (on == toneOn) return;
    toneOn = on;
    if (on) tone(PIN_BUZZER, 2700); else noTone(PIN_BUZZER);
    return;
  }
  if (toneOn) { noTone(PIN_BUZZER); toneOn = false; pinMode(PIN_BUZZER, OUTPUT); }
  bool level = buzzerType == "ACTIVE_LOW" ? !on : on;
  digitalWrite(PIN_BUZZER, level ? HIGH : LOW);
}

void beep(int times, int ms = 120) {
  for (int i = 0; i < times; i++) { setBuzzer(true); delay(ms); setBuzzer(false); delay(ms); }
}

bool clockOk() { return time(nullptr) > 1700000000; }
long long nowMs() { return (long long)time(nullptr) * 1000LL; }

int dateKey() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return 0;
  return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
}

bool isAfterHours() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return false;
  return t.tm_hour < labOpenHour || t.tm_hour >= labCloseHour;
}

String fbUrl(const String& path) {
  String url = String(FIREBASE_URL) + path + ".json";
  if (strlen(FIREBASE_AUTH) > 0) url += String("?auth=") + FIREBASE_AUTH;
  return url;
}

// One Firebase REST call over a kept-alive HTTPS connection. method = GET / PUT / PATCH / POST
bool firebase(const char* method, const String& path, const String& body = "", String* reply = nullptr) {
  if (WiFi.status() != WL_CONNECTED) return false;
  http.setReuse(true);
  http.setTimeout(5000);
  if (!http.begin(tls, fbUrl(path))) { tls.stop(); return false; }
  int code;
  if (strcmp(method, "GET") == 0) code = http.GET();
  else { http.addHeader("Content-Type", "application/json"); code = http.sendRequest(method, body); }
  bool ok = (code == 200);
  if (ok && reply) *reply = http.getString();
  if (!ok) Serial.printf("[Firebase] %s %s -> HTTP %d\n", method, path.c_str(), code);
  http.end();
  if (code < 0) tls.stop();
  return ok;
}

void raiseAlert(int type, const String& kind, const String& msg) {
  if (lastAlertMs[type] != 0 && millis() - lastAlertMs[type] < 5UL * 60UL * 1000UL) return;
  lastAlertMs[type] = millis();
  Serial.println("[ALERT] " + msg);
  JsonDocument d;
  d["type"] = kind;
  d["msg"] = msg;
  d["source"] = "ESP32";
  d["t"][".sv"] = "timestamp";
  String body; serializeJson(d, body);
  firebase("POST", "/smartlab/alerts", body);
}

// =====================================================================
//                           SENSORS + LOGIC
// =====================================================================
void readSensors() {
  unsigned long now = millis();

  // Camera (ignored when the laptop script stopped sending)
  cameraFresh = false;
  if (cameraLastSeenMs > 0) {
    cameraFresh = clockOk() ? (nowMs() - cameraLastSeenMs < (long long)CAMERA_STALE_SEC * 1000LL) : true;
  }
  camera = cameraFresh && cameraReported;

  // PIR: warm-up, relay-noise blanking, and OUT must stay HIGH for PIR_CONFIRM_MS
  bool raw = PIR_INSTALLED && digitalRead(PIN_PIR) == HIGH;
  if (raw != pirRaw) { pirRaw = raw; Serial.printf("[PIR] OUT pin G13 = %s%s\n", raw ? "HIGH (motion)" : "LOW",
                                         now < PIR_WARMUP_SEC * 1000UL ? "  (warm-up, ignored)" : ""); }
  if (!PIR_INSTALLED || now < PIR_WARMUP_SEC * 1000UL || now - lastRelayChangeMs < PIR_RELAY_IGNORE_MS) {
    pir = false; pirHighSince = 0;
  } else if (digitalRead(PIN_PIR) == HIGH) {
    if (pirHighSince == 0) pirHighSince = now;
    pir = now - pirHighSince >= PIR_CONFIRM_MS;
  } else {
    pir = false; pirHighSince = 0;
  }

  // Door: magnet near = closed = LOW
  bool dr = digitalRead(PIN_DOOR) == HIGH;
  if (dr != doorRaw) {
    Serial.printf("[DOOR] pin G27 = %s -> door %s\n", dr ? "HIGH (magnet away)" : "LOW (magnet near)",
                  (doorInvert ? !dr : dr) ? "OPEN" : "CLOSED");
    syncNow = true;                                   // send to the dashboard straight away
  }
  doorRaw = dr;
  doorOpen = doorInvert ? !doorRaw : doorRaw;
}

void updateOccupancy() {
  unsigned long now = millis();
  unsigned long timeoutMs = EMPTY_TIMEOUT_SEC * 1000UL;
  bool doorChanged = doorOpen != lastDoorOpen;
  lastDoorOpen = doorOpen;

  bool activity = camera || pir || doorChanged;
  if (activity) {
    if (!occupied && isAfterHours())
      raiseAlert(AL_AFTER_HOURS, "UNAUTHORIZED_ENTRY", "Someone entered the lab after college hours");
    lastActivityMs = now;
    everActivity = true;
  }
  bool live = camera || pir;
  bool newOcc = live || (everActivity && now - lastActivityMs < timeoutMs);
  if (newOcc != occupied) { occupied = newOcc; syncNow = true; }

  emptyTimerLeft = 0;
  if (occupied && !live) {
    long left = (long)(timeoutMs - (now - lastActivityMs)) / 1000L;
    emptyTimerLeft = left > 0 ? left : 0;
  }

  // People count = faces seen by the camera (PIR alone can only say "at least 1")
  int newCount = camera ? max(cameraPeople, 1) : (pir ? 1 : 0);
  if (newCount != peopleCount) { peopleCount = newCount; syncNow = true; }

  // Crowd alert
  bool newCrowd = camera && cameraPeople > MAX_PEOPLE;
  if (newCrowd && !crowd) {
    crowdSince = now;
    raiseAlert(AL_CROWD, "CROWD", String(cameraPeople) + " people detected in the lab (limit " + MAX_PEOPLE + ")");
  }
  if (newCrowd != crowd) { crowd = newCrowd; syncNow = true; }
}

// Crowd buzzer pattern (non-blocking): 3 short beeps every 5 seconds while crowded AND Exam Mode is ON
void updateBuzzer() {
  if (!crowd || !examMode) { setBuzzer(false); return; }
  unsigned long phase = (millis() - crowdSince) % 5000UL;
  setBuzzer(phase < 900 && (phase % 300) < 150);
}

void applyOutputs() {
  bool wantLight, wantFan;
  if (cmdMode == "MANUAL") { wantLight = cmdLight; wantFan = cmdFan; }
  else                     { wantLight = occupied; wantFan = occupied; }
  if (wantLight != lightOn || wantFan != fanOn) { syncNow = true; lastRelayChangeMs = millis(); }
  lightOn = wantLight;
  fanOn = wantFan;
  setRelayPin(PIN_RELAY_LIGHT, lightOn);
  setRelayPin(PIN_RELAY_FAN, fanOn);
}

float readVolts(int pin) {
  uint32_t mv = 0;
  for (int i = 0; i < 32; i++) mv += analogReadMilliVolts(pin);
  return mv / 32.0 / 1000.0;
}

void readMeter() {
  unsigned long now = millis();
  if (METER_INSTALLED) {
    float supply = readVolts(PIN_VOLT_SENSE) * DIVIDER_RATIO;
    meterOk = supply >= 3.0;
    vVolt = meterOk ? supply : 0;
    float ledV = readVolts(PIN_LED_SENSE);
    ledAmp = (meterOk && ledV > 0.05) ? ledV / LED_SENSE_OHMS : 0;
  } else {
    meterOk = true;
    vVolt = SUPPLY_VOLTS;
    ledAmp = lightOn ? LED_EST_AMPS : 0;
  }
  fanAmp = fanOn ? FAN_RATED_AMPS : 0;
  vAmp = ledAmp + fanAmp;
  vWatt = vVolt * vAmp;

  // Daily counter (resets at midnight, kept across reboots)
  int key = dateKey();
  if (key && key != todayKey) { todayKey = key; dcKwhToday = 0; }
  if (lastMeterMs > 0) dcKwhToday += vWatt * (now - lastMeterMs) / 3600000.0 / 1000.0;
  lastMeterMs = now;

  totalW = vWatt + pcPowerW;
  totalKwhToday = dcKwhToday + pcKwhToday;
  costToday = totalKwhToday * tariff;
  if (totalW > highPowerW) raiseAlert(AL_HIGH_POWER, "HIGH_POWER", "High lab power: " + String(totalW, 0) + " W");
}

void saveDaily() {
  prefs.putInt("day", todayKey);
  prefs.putDouble("dcKwh", dcKwhToday);
}

// =====================================================================
//                              FIREBASE SYNC
// =====================================================================
void pullCommands() {
  String reply;
  if (!firebase("GET", "/smartlab/commands", "", &reply)) { cloudOk = false; return; }
  cloudOk = true;
  if (reply == "null") return;
  JsonDocument d;
  if (deserializeJson(d, reply)) return;
  cmdMode  = d["mode"]  | cmdMode.c_str();
  cmdLight = d["light"] | cmdLight;
  cmdFan   = d["fan"]   | cmdFan;
  examMode = d["examMode"] | examMode;
  String bz = d["hwBuzzer"] | buzzerType.c_str();
  if (bz != buzzerType && (bz == "ACTIVE" || bz == "ACTIVE_LOW" || bz == "PASSIVE")) {
    setBuzzer(false); buzzerType = bz; setBuzzer(false);
    Serial.println("[HW] buzzer type -> " + buzzerType);
  }
  bool inv = d["hwDoorInvert"] | doorInvert;
  if (inv != doorInvert) { doorInvert = inv; Serial.printf("[HW] door invert -> %s\n", inv ? "yes" : "no"); }
  String bt = d["buzzerTest"] | "";
  if (bt.length() && bt != buzzerTestId) {
    bool first = buzzerTestId.length() == 0;
    buzzerTestId = bt;
    if (!first) { Serial.println("[BUZZER] test from dashboard"); beep(3, 150); }   // skip the old value at boot
  }
  cameraReported   = d["cameraPresent"]  | false;
  cameraPeople     = d["cameraPeople"]   | 0;
  cameraLastSeenMs = d["cameraLastSeen"] | (long long)0;
  int t = d["timeoutSec"] | (int)EMPTY_TIMEOUT_SEC;
  if (t >= 5 && t <= 3600) EMPTY_TIMEOUT_SEC = t;
  if (d["resetEnergy"] | false) {
    dcKwhToday = 0; saveDaily();
    firebase("PATCH", "/smartlab/commands", "{\"resetEnergy\":false}");
  }
}

// Sum of all real PC agents (same rule as the dashboard: offline after 3 missed reports / 30 s)
void pullComputers() {
  String reply;
  if (!firebase("GET", "/smartlab/computers", "", &reply)) return;
  JsonDocument filter;
  JsonObject f = filter["*"].to<JsonObject>();
  f["status"] = true; f["powerW"] = true; f["energyTodayKwh"] = true;
  f["lastSeen"] = true; f["interval"] = true; f["simulated"] = true;
  JsonDocument d;
  if (deserializeJson(d, reply, DeserializationOption::Filter(filter))) return;
  int online = 0, active = 0, idle = 0, total = 0;
  float w = 0; double kwh = 0;
  for (JsonPair kv : d.as<JsonObject>()) {
    JsonObject pc = kv.value();
    if (pc["simulated"] | false) continue;
    total++;
    kwh += pc["energyTodayKwh"] | 0.0;
    const char* st = pc["status"] | "OFFLINE";
    long long seen = pc["lastSeen"] | (long long)0;
    long long limit = max(3LL * (long long)(pc["interval"] | 10) * 1000LL, 30000LL);
    bool on = strcmp(st, "OFFLINE") != 0 && (!clockOk() || nowMs() - seen < limit);
    if (!on) continue;
    online++;
    if (strcmp(st, "IDLE") == 0) idle++; else active++;
    w += pc["powerW"] | 0.0;
  }
  pcOnline = online; pcActive = active; pcIdle = idle; pcTotal = total;
  pcPowerW = w; pcKwhToday = kwh; pcDataOk = true;
}

void pullSettings() {
  String reply;
  if (!firebase("GET", "/smartlab/settings", "", &reply) || reply == "null") return;
  JsonDocument d;
  if (deserializeJson(d, reply)) return;
  tariff       = d["tariff"]       | tariff;
  highPowerW   = d["highPowerW"]   | highPowerW;
  labOpenHour  = d["labOpenHour"]  | labOpenHour;
  labCloseHour = d["labCloseHour"] | labCloseHour;
  int mp = d["maxPeople"] | MAX_PEOPLE;
  if (mp >= 1 && mp <= 200) MAX_PEOPLE = mp;
}

void pushState() {
  JsonDocument d;
  if (PIR_INSTALLED) d["sensors/pir"] = pir;
  else d["sensors/pir"] = nullptr;                  // not fitted -> hidden on the dashboard
  d["sensors/camera"] = cameraFresh ? (camera ? "PERSON" : "EMPTY") : "OFFLINE";
  d["sensors/door"]   = doorOpen ? "OPEN" : "CLOSED";
  d["sensors/mmwave"] = nullptr;                    // removed sensor

  d["hardware/light"]          = lightOn;
  d["hardware/fan"]            = fanOn;
  d["hardware/mode"]           = cmdMode;
  d["hardware/occupied"]       = occupied;
  d["hardware/occupancyCount"] = peopleCount;
  d["hardware/crowd"]          = crowd;
  d["hardware/examMode"]       = examMode;
  if (PIR_INSTALLED) d["hardware/diag/pirPin"] = pirRaw;   // Hardware Test panel (raw pin levels)
  else d["hardware/diag/pirPin"] = nullptr;
  d["hardware/diag/pirWarmup"] = millis() < PIR_WARMUP_SEC * 1000UL ? (long)(PIR_WARMUP_SEC - millis() / 1000UL) : 0;
  d["hardware/diag/doorPin"]   = doorRaw;
  d["hardware/diag/doorInvert"]= doorInvert;
  d["hardware/diag/buzzer"]    = buzzerType;
  d["hardware/diag/uptimeSec"] = (long)(millis() / 1000UL);
  d["hardware/maxPeople"]      = MAX_PEOPLE;
  d["hardware/emptyTimerSec"]  = emptyTimerLeft;
  d["hardware/timeoutSec"]     = (int)EMPTY_TIMEOUT_SEC;
  d["hardware/wifiRssi"]       = WiFi.RSSI();
  d["hardware/device"]         = "ESP32";
  d["hardware/lastSeen"][".sv"]= "timestamp";
  d["hardware/fanSpeed"]       = nullptr;           // removed features
  d["hardware/source"]         = nullptr;
  d["hardware/realOccupied"]   = nullptr;
  d["hardware/virtualOccupied"]= nullptr;

  d["energy/ok"]            = meterOk;
  d["energy/type"]          = "DC";
  d["energy/sensor"]        = METER_INSTALLED ? "ADC" : "ESTIMATED";
  d["energy/voltage"]       = vVolt;
  d["energy/current"]       = vAmp;
  d["energy/power"]         = vWatt;               // lights + fan
  d["energy/ledCurrent"]    = ledAmp;
  d["energy/fanCurrentEst"] = fanAmp;
  d["energy/energyKwh"]     = dcKwhToday;          // lights + fan today
  d["energy/pcPowerW"]      = pcPowerW;
  d["energy/pcOnline"]      = pcOnline;
  d["energy/pcActive"]      = pcActive;
  d["energy/pcIdle"]        = pcIdle;
  d["energy/pcTotal"]       = pcTotal;
  d["energy/pcKwhToday"]    = pcKwhToday;
  d["energy/totalPowerW"]   = totalW;
  d["energy/totalKwhToday"] = totalKwhToday;
  d["energy/costToday"]     = costToday;
  d["energy/tariff"]        = tariff;
  d["energy/frequency"]     = nullptr;
  d["energy/pf"]            = nullptr;

  String body; serializeJson(d, body);
  cloudOk = firebase("PATCH", "/smartlab", body);
}

void logEnergy() {
  JsonDocument d;
  d["t"][".sv"] = "timestamp";
  d["p"]  = totalW;           // total lab power (PCs + lights + fan)
  d["e"]  = totalKwhToday;    // total energy today (resets at midnight)
  d["lf"] = vWatt;            // lights + fan only
  d["pc"] = pcPowerW;         // computers only
  d["o"]  = occupied ? 1 : 0;
  d["c"]  = peopleCount;
  d["l"]  = lightOn ? 1 : 0;
  d["f"]  = fanOn ? 1 : 0;
  String body; serializeJson(d, body);
  firebase("POST", "/smartlab/energyLog", body);
}

// =====================================================================
//                                 OLED  (5 pages in sequence)
// =====================================================================
const int OLED_PAGES = 5;

// One text row of the OLED: fixed position, cut to 21 characters so nothing wraps onto the next row.
void row(int n, const char* fmt, ...) {
  char buf[40];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  buf[21] = 0;
  oled.setCursor(0, 14 + n * 8);
  oled.print(buf);
}

// Same rounding as the dashboard: < 10 W -> 2 decimals, < 1000 W -> 1 decimal, else kW
String wattStr(float w) {
  char b[16];
  if (w < 10) snprintf(b, sizeof(b), "%.2f W", w);
  else if (w < 1000) snprintf(b, sizeof(b), "%.1f W", w);
  else snprintf(b, sizeof(b), "%.2f kW", w / 1000.0);
  return String(b);
}

// Energy: small values in Wh, bigger in kWh
String energyStr(double kwh) {
  char b[16];
  double wh = kwh * 1000.0;
  if (wh < 10) snprintf(b, sizeof(b), "%.2f Wh", wh);
  else if (wh < 1000) snprintf(b, sizeof(b), "%.1f Wh", wh);
  else snprintf(b, sizeof(b), "%.3f kWh", kwh);
  return String(b);
}

void header(const char* title) {
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(title);
  oled.setCursor(104, 0);
  oled.printf("%d/%d", oledPage + 1, OLED_PAGES);
  oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);
}

void drawOled() {
  if (!OLED_INSTALLED) return;
  if (millis() - oledPageAt >= OLED_PAGE_SEC * 1000UL) { oledPage = (oledPage + 1) % OLED_PAGES; oledPageAt = millis(); }
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  bool wifi = WiFi.status() == WL_CONNECTED;

  if (crowd) {                              // crowd alert overrides the pages
    oled.setCursor(0, 0); oled.print("!!  CROWD ALERT  !!");
    oled.setTextSize(2); oled.setCursor(0, 18); oled.printf("%d people", cameraPeople);
    oled.setTextSize(1);
    oled.setCursor(0, 44); oled.printf("Limit: %d", MAX_PEOPLE);
    oled.setCursor(0, 55); oled.print(examMode ? "Exam mode: buzzer ON" : "Exam OFF: silent");
    oled.display();
    return;
  }

  switch (oledPage) {
    case 0: {
      header("LAB STATUS");
      row(0, "Mode:%s Exam:%s", cmdMode.c_str(), examMode ? "ON" : "OFF");
      row(1, "People: %d %s", peopleCount, occupied ? "OCCUPIED" : "EMPTY");
      row(2, "Camera: %s", !cloudOk ? "--" : cameraFresh ? (camera ? "PERSON" : "no one") : "OFFLINE");
      if (!PIR_INSTALLED) { /* PIR not fitted: row not shown */ }
      else if (millis() < PIR_WARMUP_SEC * 1000UL) row(3, "PIR   : warm-up %lus", PIR_WARMUP_SEC - millis() / 1000UL);
      else row(3, "PIR   : %s", pir ? "MOTION" : (pirRaw ? "HIGH..." : "no motion"));
      row(4, "Door  : %s", doorOpen ? "OPEN" : "CLOSED");
      if (emptyTimerLeft > 0) row(5, "Auto OFF in %lds", emptyTimerLeft);
      break;
    }
    case 1:
      header("LIGHT & FAN");
      row(0, "Light : %s", lightOn ? "ON" : "OFF");
      row(1, "Fan   : %s", fanOn ? "ON" : "OFF");
      if (!meterOk) { row(2, "Meter : no 5V (G35)"); break; }
      row(2, "Volt  : %.2f V%s", vVolt, METER_INSTALLED ? "" : " (est)");
      row(3, "Curr  : %.0f mA", vAmp * 1000);
      row(4, "Power : %s", wattStr(vWatt).c_str());
      row(5, "Today : %s", energyStr(dcKwhToday).c_str());
      break;
    case 2:
      header("COMPUTERS");
      if (!pcDataOk) { row(0, wifi ? "waiting for data..." : "no Wi-Fi"); break; }
      row(0, "Online: %d of %d", pcOnline, pcTotal);
      row(1, "Active: %d Idle: %d", pcActive, pcIdle);
      row(2, "Power : %s", wattStr(pcPowerW).c_str());
      row(3, "Today : %s", energyStr(pcKwhToday).c_str());
      row(4, "Cost  : Rs %.2f", pcKwhToday * tariff);
      break;
    case 3:
      header("TOTAL LAB");
      row(0, "Now   : %s", wattStr(totalW).c_str());
      row(1, "Today : %s", energyStr(totalKwhToday).c_str());
      row(2, "Cost  : Rs %.2f", costToday);
      row(3, "PCs   : %s", pcDataOk ? wattStr(pcPowerW).c_str() : "--");
      row(4, "Lt+Fan: %s", wattStr(vWatt).c_str());
      if (!pcDataOk) row(5, "(PC data not loaded)");
      break;
    default:
      header("NETWORK");
      row(0, "WiFi  : %s", wifi ? "OK" : "not connected");
      row(1, "Signal: %s", wifi ? (String(WiFi.RSSI()) + " dBm").c_str() : "--");
      row(2, "Cloud : %s", cloudOk ? "OK" : (wifi ? "NO INTERNET?" : "--"));
      row(3, "IP    : %s", wifi ? WiFi.localIP().toString().c_str() : "--");
      row(4, "Clock : %s", clockOk() ? "synced" : "waiting");
      row(5, "Up    : %lu min", millis() / 60000UL);
      break;
  }
  oled.display();
}

// =====================================================================
//                                SETUP
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== SmartLab Guardian starting ===");

  setRelayPin(PIN_RELAY_LIGHT, false);
  setRelayPin(PIN_RELAY_FAN, false);
  pinMode(PIN_RELAY_LIGHT, OUTPUT);
  pinMode(PIN_RELAY_FAN, OUTPUT);
  setRelayPin(PIN_RELAY_LIGHT, false);
  setRelayPin(PIN_RELAY_FAN, false);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_DOOR, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_CLOUD, OUTPUT);
  setBuzzer(false);
  analogSetPinAttenuation(PIN_LED_SENSE, ADC_11db);
  analogSetPinAttenuation(PIN_VOLT_SENSE, ADC_11db);

  prefs.begin("smartlab", false);
  todayKey = prefs.getInt("day", 0);
  dcKwhToday = prefs.getDouble("dcKwh", 0);

  Wire.begin(21, 22);
  if (OLED_INSTALLED) {
    if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("OLED not found (SDA=21, SCL=22, address 0x3C)");
    oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); oled.setTextSize(1);
    oled.setTextWrap(false);                // long text is cut, never wrapped onto the next row
    oled.setCursor(0, 0); oled.println("SmartLab Guardian"); oled.println("Relay test...");
    oled.display();
  }

  // Relay self-test: LED strip 1 s, fan 1 s
  setRelayPin(PIN_RELAY_LIGHT, true); delay(1000); setRelayPin(PIN_RELAY_LIGHT, false);
  setRelayPin(PIN_RELAY_FAN, true);   delay(1000); setRelayPin(PIN_RELAY_FAN, false);
  lastRelayChangeMs = millis();

  if (OLED_INSTALLED) {
    oled.clearDisplay(); oled.setCursor(0, 0);
    oled.println("SmartLab Guardian"); oled.println("Connecting Wi-Fi:"); oled.println(WIFI_NAME);
    oled.display();
  }
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);     // smaller current spikes -> no brownout resets on USB power
  WiFi.begin(WIFI_NAME, WIFI_PASSWORD);
  Serial.print("Wi-Fi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) { delay(400); Serial.print("."); }
  Serial.println(WiFi.status() == WL_CONNECTED ? " connected, IP " + WiFi.localIP().toString() : " FAILED (will keep retrying)");
  if (OLED_INSTALLED) {
    oled.println(WiFi.status() == WL_CONNECTED ? "Wi-Fi OK" : "Wi-Fi FAILED");
    oled.println("Checking Firebase...");
    oled.display();
  }

  tls.setInsecure();
  configTime(19800, 0, "pool.ntp.org", "time.google.com");   // India time (UTC+5:30)

  readSensors();
  lastDoorOpen = doorOpen;
  Serial.println("Buzzer test: 2 beeps");
  beep(2, 150);
  Serial.printf("Presence: camera + PIR(%s) + door | light/fan power: %s | empty timeout %lus | crowd limit %d\n",
                PIR_INSTALLED ? "G13, 60 s warm-up" : "off", METER_INSTALLED ? "measured" : "estimated",
                EMPTY_TIMEOUT_SEC, MAX_PEOPLE);
}

// =====================================================================
//                                 LOOP
// =====================================================================
void loop() {
  unsigned long now = millis();

  if (now - tSensors >= 100) {
    tSensors = now;
    readSensors();
    updateOccupancy();
    applyOutputs();
    updateBuzzer();
    digitalWrite(PIN_LED_CLOUD, cloudOk ? HIGH : LOW);
  }
  if (now - tMeter >= 1000) { tMeter = now; readMeter(); }
  if (now - tOled >= 250)   { tOled = now; drawOled(); }
  if (now - tSave >= 60000) { tSave = now; saveDaily(); }

  if (WiFi.status() != WL_CONNECTED) {
    cloudOk = false;
    if (now - tWifi >= 10000) { tWifi = now; Serial.println("Wi-Fi lost, reconnecting..."); WiFi.reconnect(); }
    return;
  }

  if (now - tSettings >= 60000 || tSettings == 0) { tSettings = now; pullSettings(); }
  if (now - tPcs >= 10000 || tPcs == 0)           { tPcs = now; pullComputers(); }
  if (now - tSync >= 2000 || syncNow) {
    tSync = now;
    syncNow = false;
    pullCommands();
    readSensors();
    updateOccupancy();
    applyOutputs();
    readMeter();
    pushState();
  }
  if (now - tLog >= 60000) { tLog = now; logEnergy(); }
}
