#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#if !__has_include("config.h")
#error "Missing include/config.h: copy include/config.h.example to include/config.h and fill in your values"
#endif

#include "config.h"
#include "settings.h"
#include "auth.h"
#include "json_util.h"

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

WebServer   server(80);
Preferences prefs;

bool    lampOn = false;
bool    autoMode = true;
uint8_t manualBrightness = 128;
uint8_t autoTarget = 0;
uint8_t outDuty = 0;

float ldrSmoothed = 0;
bool  ldrInit = false;
int   ldrRaw = 0, ambientPct = 0;
float ldrLo = 0, ldrHi = 0;
bool  calReady = false;
float calSavedLo = -1, calSavedHi = -1;

bool     pir1 = false, occupied = false;
bool     pirRawPrev = false, pirLowSeen = false;
uint32_t pirRawChgMs = 0, pirFellMs = 0, pirHighSinceMs = 0, pirStuckLogMs = 0, lastMotionMs = 0;

float    volts = SUPPLY_VOLTS, amps = 0, watts = 0, avgW10 = 0;
double   wh = 0, lastSavedWh = 0;
uint64_t runMs = 0;
float    wBuf[AVG_SAMPLES];
int      wIdx = 0, wCnt = 0;

uint32_t lastTickMs = 0, lastSaveMs = 0, lastLogMs = 0;

bool     wifiConnecting = false, wifiWasUp = false;
uint32_t wifiAttemptMs = 0;
uint32_t activityUntil = 0;
uint8_t  ledR = 255, ledG = 255, ledB = 255;

void flashActivity(uint16_t ms = 180) { activityUntil = millis() + ms; }

void updateStatusLed(uint32_t now) {
  uint8_t r = 0, g = 0, b = 0;
  if (WiFi.status() == WL_CONNECTED) g = RGB_LEVEL;
  else if (wifiConnecting) { if ((now / 400) % 2 == 0) r = RGB_LEVEL; }
  else r = RGB_LEVEL;
  if ((int32_t)(activityUntil - now) > 0) { r = 0; g = 0; b = RGB_LEVEL; }
  if (r != ledR || g != ledG || b != ledB) {
    ledR = r; ledG = g; ledB = b;
    neopixelWrite(PIN_RGB, r, g, b);
  }
}

void addCors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, ngrok-skip-browser-warning");
  server.sendHeader("Access-Control-Max-Age", "600");
}

void sendJson(int code, const String& body) {
  addCors();
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

void sendError(int code, const char* msg) {
  Serial.printf("[HTTP] -> %d %s\n", code, msg);
  String b = "{\"ok\":false,\"error\":\"";
  b += msg;
  b += "\"}";
  sendJson(code, b);
}

uint8_t targetDuty() {
  if (!lampOn) return 0;
  return autoMode ? autoTarget : manualBrightness;
}

void updateDuty() {
  uint8_t tgt = targetDuty(), prev = outDuty;
  if (!lampOn || !autoMode) outDuty = tgt;
  else if (outDuty == 0)    outDuty = tgt;
  else if (tgt > outDuty)   outDuty = (uint8_t)min((int)tgt, (int)outDuty + AUTO_SLEW_STEP);
  else if (tgt < outDuty)   outDuty = (uint8_t)max((int)tgt, (int)outDuty - AUTO_SLEW_STEP);
  if (outDuty != prev && (outDuty == tgt || outDuty == 0 || outDuty == 255))
    Serial.printf("[OUT] duty %u -> %u (%d%%) target=%u\n", prev, outDuty, (int)lroundf(outDuty / 2.55f), tgt);
}

void pwmWriteDuty(uint8_t duty) {
  uint8_t out = PWM_INVERTED ? (uint8_t)(255 - duty) : duty;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_PWM, out);
#else
  ledcWrite(0, out);
#endif
}

void applyOutputs() { pwmWriteDuty(outDuty); }

void setLamp(bool on, const char* src) {
  bool changed = (on != lampOn);
  lampOn = on;
  Serial.printf("[LAMP] %s by %s%s\n", on ? "ON" : "OFF", src, changed ? "" : " (no change)");
  if (changed) flashActivity();
}

void loadCal() {
  prefs.begin("ldrcal", true);
  if (prefs.isKey("lo") && prefs.isKey("hi")) {
    ldrLo = prefs.getFloat("lo", 0);
    ldrHi = prefs.getFloat("hi", 0);
    calReady = (ldrHi > ldrLo);
  }
  prefs.end();
  calSavedLo = ldrLo;
  calSavedHi = ldrHi;
  if (calReady) Serial.printf("[NVS] LDR calibration loaded: %.0f .. %.0f\n", ldrLo, ldrHi);
}

void saveCal() {
  if (!calReady) return;
  if (fabsf(ldrLo - calSavedLo) < 10 && fabsf(ldrHi - calSavedHi) < 10) return;
  prefs.begin("ldrcal", false);
  prefs.putFloat("lo", ldrLo);
  prefs.putFloat("hi", ldrHi);
  prefs.end();
  calSavedLo = ldrLo;
  calSavedHi = ldrHi;
  Serial.printf("[NVS] LDR calibration saved: %.0f .. %.0f\n", ldrLo, ldrHi);
}

void resetCal() {
  prefs.begin("ldrcal", false);
  prefs.clear();
  prefs.end();
  calReady = false;
  ldrInit = false;
  calSavedLo = calSavedHi = -1;
  Serial.println("[LDR] calibration reset");
}

void loadEnergy() {
  prefs.begin("energy", false);
  wh = prefs.getDouble("wh", 0);
  runMs = prefs.getULong64("run", 0);
  prefs.end();
  lastSavedWh = wh;
  Serial.printf("[NVS] loaded energy %.5f Wh, runtime %.5f h\n", wh, (double)runMs / 3600000.0);
}

void saveEnergy() {
  if (wh == lastSavedWh) return;
  prefs.begin("energy", false);
  prefs.putDouble("wh", wh);
  prefs.putULong64("run", runMs);
  prefs.end();
  lastSavedWh = wh;
  Serial.printf("[NVS] saved energy %.5f Wh\n", wh);
}

void pollSwitch(uint32_t now) {
  static bool lastRaw = false, stable = false, init = false;
  static uint32_t chgMs = 0;
  bool raw = (digitalRead(PIN_SWITCH) == (SWITCH_ACTIVE_LOW ? LOW : HIGH));
  if (!init) { init = true; lastRaw = stable = raw; }
  if (raw != lastRaw) { lastRaw = raw; chgMs = now; }
  if (raw != stable && now - chgMs >= DEBOUNCE_MS) {
    stable = raw;
    Serial.printf("[INPUT] switch flipped -> %s\n", stable ? "CLOSED" : "OPEN");
    if (SWITCH_RETURNS_TO_AUTO && !autoMode) { autoMode = true; Serial.println("[MODE] AUTO (switch)"); }
    bool turnOn = !lampOn;
    if (turnOn && !autoMode && manualBrightness == 0) manualBrightness = SWITCH_ON_DUTY;
    setLamp(turnOn, "mechanical switch");
    updateDuty();
    applyOutputs();
  }
}

void readLdr() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogRead(PIN_LDR);
  int raw = (int)(sum / 16);
  if (!LDR_HIGH_IS_BRIGHT) raw = 4095 - raw;
  ldrRaw = raw;
  if (!ldrInit) { ldrSmoothed = raw; ldrInit = true; }
  else ldrSmoothed = ldrSmoothed * 0.95f + raw * 0.05f;

  float lo, hi;
  if (LDR_AUTO_CAL) {
    if (!calReady) {
      ldrLo = fmaxf(0.0f, ldrSmoothed - LDR_MIN_SPAN / 2);
      ldrHi = fminf(4095.0f, ldrLo + LDR_MIN_SPAN);
      calReady = true;
      Serial.printf("[LDR] learning started around %.0f\n", ldrSmoothed);
    }
    if (ldrSmoothed < ldrLo) ldrLo = ldrSmoothed;
    if (ldrSmoothed > ldrHi) ldrHi = ldrSmoothed;
    lo = ldrLo;
    hi = fmaxf(ldrHi, ldrLo + LDR_MIN_SPAN);
  } else {
    lo = LDR_DARK_RAW;
    hi = LDR_BRIGHT_RAW;
  }
  float pct = (hi > lo) ? (ldrSmoothed - lo) * 100.0f / (hi - lo) : 0;
  ambientPct = (int)lroundf(fminf(100.0f, fmaxf(0.0f, pct)));

  uint8_t prevT = autoTarget;
  int want = ((100 - ambientPct) * 255) / 100;
  if (want < AUTO_MIN_DUTY) want = AUTO_MIN_DUTY;
  if (autoTarget == 0 || abs(want - (int)autoTarget) >= AUTO_DEADBAND) autoTarget = (uint8_t)want;
  if (autoTarget != prevT && autoMode && lampOn && abs((int)autoTarget - (int)prevT) >= 8) flashActivity();
}

void readPir(uint32_t now) {
  bool pr = digitalRead(PIN_PIR1);
  if (pr != pirRawPrev) { pirRawPrev = pr; pirRawChgMs = now; }
  if (pr != pir1 && now - pirRawChgMs >= PIR_FILTER_MS) {
    pir1 = pr;
    flashActivity();
    if (pir1) {
      bool validEdge = pirLowSeen && (now - pirFellMs >= PIR_MIN_LOW_MS);
      pirHighSinceMs = now;
      pirStuckLogMs = now;
      lastMotionMs = now;
      pirLowSeen = false;
      Serial.printf("[INPUT] PIR motion%s\n", validEdge ? "" : " (not a valid trigger)");
      if (validEdge && autoMode && !lampOn) {
        if (now < PIR_WARMUP_MS) Serial.printf("[AUTO] motion ignored, PIR warming up (%lu s left)\n", (unsigned long)((PIR_WARMUP_MS - now) / 1000UL));
        else setLamp(true, "PIR motion");
      }
    } else {
      pirLowSeen = true;
      pirFellMs = now;
      Serial.println("[INPUT] PIR clear");
    }
  }
  if (pir1) {
    lastMotionMs = now;
    if (now - pirStuckLogMs >= PIR_STUCK_WARN_MS) {
      pirStuckLogMs = now;
      Serial.printf("[PIR] WARNING: output HIGH for %lu s, check sensitivity, delay pots and supply\n", (unsigned long)((now - pirHighSinceMs) / 1000UL));
    }
  }
  occupied = pir1 || (now - lastMotionMs < OCCUPANCY_HOLD_MS);
}

void readSensors(uint32_t now) {
  readLdr();
  readPir(now);
}

void updatePower(uint32_t dtMs) {
  bool lit = outDuty > 0;
  volts = SUPPLY_VOLTS;
  watts = lit ? LED_MAX_WATTS * (float)outDuty / 255.0f : 0;
  amps = volts > 0 ? watts / volts : 0;
  wh += (double)watts * (double)dtMs / 3600000.0;
  if (lit) runMs += dtMs;
  wBuf[wIdx] = watts;
  wIdx = (wIdx + 1) % AVG_SAMPLES;
  if (wCnt < AVG_SAMPLES) wCnt++;
  float s = 0;
  for (int i = 0; i < wCnt; i++) s += wBuf[i];
  avgW10 = s / wCnt;
}

String buildStatusJson() {
  char buf[640];
  snprintf(buf, sizeof(buf),
    "{\"ok\":true,\"online\":true,\"mode\":\"%s\",\"brightness\":%u,\"manualBrightness\":%u,"
    "\"power\":{\"volts\":%.2f,\"amps\":%.4f,\"watts\":%.3f,\"avgW10\":%.3f,\"runtimeH\":%.5f,\"wh\":%.5f},"
    "\"sensors\":{\"ldr\":%d,\"ambientPct\":%d,\"pir1\":%s,\"motion\":%s,\"lamp\":%s},"
    "\"uptimeSec\":%lu,\"rssi\":%d,\"ip\":\"%s\"}",
    autoMode ? "auto" : "manual", outDuty, manualBrightness,
    volts, amps, watts, avgW10, (double)runMs / 3600000.0, wh,
    ldrRaw, ambientPct, pir1 ? "true" : "false", occupied ? "true" : "false", lampOn ? "true" : "false",
    (unsigned long)(millis() / 1000UL), WiFi.RSSI(), WiFi.localIP().toString().c_str());
  return String(buf);
}

void handleStatus() { sendJson(200, buildStatusJson()); }

void handleChallenge() {
  sendJson(200, String("{\"ok\":true,\"nonce\":\"") + auth::issueNonce() + "\"}");
}

void handleLogin() {
  flashActivity();
  String body = server.arg("plain"), nonce, proof;
  if (!jsonGetString(body, "nonce", nonce) || !jsonGetString(body, "proof", proof)) { sendError(400, "Bad request"); return; }
  auth::LoginResult r = auth::login(nonce, proof);
  if (r.httpCode != 200) { sendError(r.httpCode, r.error.c_str()); return; }
  Serial.println("[AUTH] login OK");
  sendJson(200, String("{\"ok\":true,\"token\":\"") + r.token + "\"}");
}

void handleControl() {
  if (!auth::tokenValid(server.header("Authorization"))) { sendError(401, "unauthorized"); return; }
  String body = server.arg("plain");
  if (body.length() == 0) { sendError(400, "Empty body"); return; }

  bool newAuto = autoMode, newLamp = lampOn;
  uint8_t newManual = manualBrightness;
  bool hasMode = false, hasLamp = false, hasBr = false;
  int s;

  if (jsonFindValue(body, "mode", s)) {
    String m;
    if (!jsonGetString(body, "mode", m) || (m != "auto" && m != "manual")) { sendError(400, "mode must be auto or manual"); return; }
    newAuto = (m == "auto");
    hasMode = true;
  }
  if (jsonFindValue(body, "lamp", s)) {
    bool l;
    if (!jsonGetBool(body, "lamp", l)) { sendError(400, "lamp must be true or false"); return; }
    newLamp = l;
    hasLamp = true;
  }
  if (jsonFindValue(body, "brightness", s)) {
    long v;
    if (!jsonGetNumber(body, "brightness", v) || v < 0 || v > 255) { sendError(400, "brightness must be 0-255"); return; }
    newManual = (uint8_t)v;
    hasBr = true;
  }
  if (hasMode && !newAuto && autoMode && !hasBr) newManual = outDuty;
  if (hasBr && !newAuto && !hasLamp) { newLamp = newManual > 0; hasLamp = true; }

  flashActivity();
  autoMode = newAuto;
  manualBrightness = newManual;
  if (hasLamp) setLamp(newLamp, "web");
  updateDuty();
  applyOutputs();
  sendJson(200, buildStatusJson());
}

void handleNotFound() {
  if (server.method() == HTTP_OPTIONS) { addCors(); server.send(204, "text/plain", ""); return; }
  sendError(404, "not found");
}

void startWifiAttempt() {
  Serial.printf("[WIFI] connecting to \"%s\"\n", WIFI_SSID);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiConnecting = true;
  wifiAttemptMs = millis();
}

void wifiService(uint32_t now) {
  bool up = (WiFi.status() == WL_CONNECTED);
  if (up) {
    if (!wifiWasUp) Serial.printf("[WIFI] connected, IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    wifiConnecting = false;
  } else if (wifiWasUp) {
    Serial.println("[WIFI] connection lost, lamp keeps running");
    startWifiAttempt();
  } else if (wifiConnecting && now - wifiAttemptMs >= WIFI_TRY_MS) {
    wifiConnecting = false;
    Serial.println("[WIFI] attempt timed out");
  } else if (!wifiConnecting && now - wifiAttemptMs >= WIFI_RETRY_MS) {
    startWifiAttempt();
  }
  wifiWasUp = up;
}

void serialService() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'c' || c == 'C') resetCal();
  }
}

void logTick(uint32_t now) {
  Serial.printf("[%8.1fs] wifi=%s mode=%s lamp=%s | ldr=%d amb=%d%% | pir=%d occ=%d | duty=%u(%d%%) | %.1fV %.4fA %.3fW avg10=%.3fW %.5fWh run=%.4fh\n",
    now / 1000.0, WiFi.status() == WL_CONNECTED ? "UP" : (wifiConnecting ? "TRY" : "DOWN"),
    autoMode ? "AUTO" : "MAN", lampOn ? "ON" : "OFF",
    ldrRaw, ambientPct, pir1, occupied, outDuty, (int)lroundf(outDuty / 2.55f),
    volts, amps, watts, avgW10, wh, (double)runMs / 3600000.0);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Room Light Controller boot ===");

  neopixelWrite(PIN_RGB, RGB_LEVEL, 0, 0);
  ledR = RGB_LEVEL; ledG = 0; ledB = 0;

  pinMode(PIN_PWM, OUTPUT);
  digitalWrite(PIN_PWM, PWM_INVERTED ? HIGH : LOW);
  pinMode(PIN_PIR1, INPUT_PULLDOWN);
  pinMode(PIN_SWITCH, SWITCH_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_PWM, PWM_FREQ, PWM_RES);
#else
  ledcSetup(0, PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_PWM, 0);
#endif
  pwmWriteDuty(0);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LDR, ADC_11db);

  auth::begin(ADMIN_PASSWORD);
  loadEnergy();
  if (LDR_AUTO_CAL) loadCal();

  delay(50);
  uint32_t t0 = millis();
  pir1 = pirRawPrev = digitalRead(PIN_PIR1);
  pirRawChgMs = t0;
  pirLowSeen = !pir1;
  pirFellMs = t0;
  pirHighSinceMs = pirStuckLogMs = lastMotionMs = t0;
  readSensors(t0);
  updateDuty();
  applyOutputs();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false);
  startWifiAttempt();

  static const char* HDRS[] = { "Authorization" };
  server.collectHeaders(HDRS, 1);
  server.on("/api/status",    HTTP_GET,  handleStatus);
  server.on("/api/challenge", HTTP_GET,  handleChallenge);
  server.on("/api/login",     HTTP_POST, handleLogin);
  server.on("/api/control",   HTTP_POST, handleControl);
  server.onNotFound(handleNotFound);
  server.begin();

  lastTickMs = lastSaveMs = lastLogMs = millis();
}

void loop() {
  uint32_t now = millis();

  wifiService(now);
  server.handleClient();
  pollSwitch(now);
  serialService();

  if (now - lastTickMs >= SENSOR_MS) {
    uint32_t dt = now - lastTickMs;
    lastTickMs = now;
    readSensors(now);
    updateDuty();
    applyOutputs();
    updatePower(dt);
  }
  if (now - lastLogMs >= LOG_MS) { lastLogMs = now; logTick(now); }
  if (now - lastSaveMs >= SAVE_MS) { lastSaveMs = now; saveEnergy(); saveCal(); }

  updateStatusLed(now);
  delay(1);
}
