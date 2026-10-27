// Web interface: own Wi-Fi network (only while parked), one page with Control, Monitor and Settings.
// This file shares all globals with sketch_sep11b.ino.

#include <WiFi.h>
#include <WebServer.h>
#include "WebPage.h"

constexpr char WIFI_SSID[] = "E39-Controls";
constexpr uint32_t WIFI_MOVING_GRACE_MS = 3000;   // keep the network up this long after the car starts moving
constexpr uint8_t WIFI_MOVING_SPEED_KMH = 5;
constexpr uint8_t WIFI_PASSWORD_MIN_LENGTH = 8;

WebServer webServer(80);
bool wifiAllowed = true;      // serial "wifi off" keeps the network down
bool wifiUp = false;
uint32_t wifiMovingSinceMs = 0;
char responseBuffer[1200];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool isCarMoving() {
  return carGauges.hasSpeedRpm && carGauges.speedKmh > 0;
}

void sendResult(bool ok, const char *message) {
  snprintf(responseBuffer, sizeof(responseBuffer), "{\"ok\":%s,\"msg\":\"%s\"}", ok ? "true" : "false", message);
  webServer.send(200, "application/json", responseBuffer);
}

bool refuseWhileMoving() {
  if (!isCarMoving()) return false;
  sendResult(false, "Refused: the car is moving");
  return true;
}

// ---------------------------------------------------------------------------
// Settings -> runtime
// ---------------------------------------------------------------------------

void applySettings() {
  unlockOnIgnitionOff = settings.unlockOnEngineOff;
  unlockAnyLock = settings.unlockAnyLock;
  lowBeamAfterIgnitionOff = settings.lowBeamsAfterOff;
  lowBeamHold.setDurationMs((uint32_t)settings.lowBeamSeconds * 1000);
  trackDisplayEnabled = settings.trackDisplay;
  trackMarquee.setStepMs(settings.scrollStepMs);
  headerGaugesEnabled = settings.headerGauges;
  for (uint8_t i = 0; i < HEADER_ZONE_COUNT; i++) {
    headerZones[i].gauge = (HeaderGauge)settings.zoneGauge[i];
  }
  memset(headerZoneLastText, 0, sizeof(headerZoneLastText));
  memset(headerZoneLastSentMs, 0, sizeof(headerZoneLastSentMs));
  reverseDuckingEnabled = settings.reverseDucking;
  attenuationPercent = settings.duckPercent;
}

// Returns an empty string when the key and value were accepted, otherwise the reason.
const char *changeSetting(const char *key, const char *value) {
  long number = atol(value);
  bool flag = number != 0;

  if (strcmp(key, "unlock") == 0) settings.unlockOnEngineOff = flag;
  else if (strcmp(key, "unlockAny") == 0) settings.unlockAnyLock = flag;
  else if (strcmp(key, "lowbeam") == 0) settings.lowBeamsAfterOff = flag;
  else if (strcmp(key, "lbSec") == 0) settings.lowBeamSeconds = clampValue(number, LOW_BEAM_SECONDS_MIN, LOW_BEAM_SECONDS_MAX);
  else if (strcmp(key, "track") == 0) settings.trackDisplay = flag;
  else if (strcmp(key, "scrollMs") == 0) settings.scrollStepMs = clampValue(number, SCROLL_STEP_MIN_MS, SCROLL_STEP_MAX_MS);
  else if (strcmp(key, "hdr") == 0) settings.headerGauges = flag;
  else if (strcmp(key, "duck") == 0) settings.reverseDucking = flag;
  else if (strcmp(key, "duckPct") == 0) settings.duckPercent = clampValue(number, 0, DUCK_PERCENT_MAX);
  else if (strcmp(key, "welcome") == 0) settings.welcomeLights = flag;
  else if (strcmp(key, "goodbye") == 0) settings.goodbyeLights = flag;
  else if (strncmp(key, "zone", 4) == 0 && key[4] >= '0' && key[4] < '0' + HEADER_ZONE_COUNT && key[5] == '\0') {
    HeaderGauge gauge;
    if (!headerGaugeFromName(value, gauge)) return "Unknown gauge";
    settings.zoneGauge[key[4] - '0'] = (uint8_t)gauge;
  } else {
    return "Unknown setting";
  }

  settingsSave(settings);
  applySettings();
  return "";
}

// ---------------------------------------------------------------------------
// HTTP handlers
// ---------------------------------------------------------------------------

void handleRoot() {
  webServer.send_P(200, "text/html", WEB_PAGE);
}

void handleLamps() {
  int length = snprintf(responseBuffer, sizeof(responseBuffer), "[");
  for (uint8_t i = 0; i < LAMP_COUNT; i++) {
    length += snprintf(responseBuffer + length, sizeof(responseBuffer) - length, "%s{\"id\":\"%s\",\"label\":\"%s\"}",
                       i ? "," : "", LAMPS[i].id, LAMPS[i].label);
  }
  snprintf(responseBuffer + length, sizeof(responseBuffer) - length, "]");
  webServer.send(200, "application/json", responseBuffer);
}

void handleState() {
  const CarGauges &g = carGauges;
  char lampBits[LAMP_COUNT + 1];
  for (uint8_t i = 0; i < LAMP_COUNT; i++) lampBits[i] = manualLamps.isOn(i) ? '1' : '0';
  lampBits[LAMP_COUNT] = '\0';

  snprintf(responseBuffer, sizeof(responseBuffer),
           "{\"ign\":%u,\"speed\":%d,\"rpm\":%d,\"coolant\":%d,\"ambient\":%d,\"odo\":%ld,"
           "\"locked\":%d,\"lockKnown\":%d,\"reverse\":%d,\"hold\":%d,\"lamps\":\"%s\",\"lampsLeft\":%lu,"
           "\"range\":\"%s\",\"cons\":\"%s\",\"avg\":\"%s\"}",
           ignitionStatus,
           g.hasSpeedRpm ? (int)g.speedKmh : -1, g.hasSpeedRpm ? (int)g.rpm : -1,
           g.hasTemperatures ? (int)g.coolantC : -999, g.hasTemperatures ? (int)g.ambientC : -999,
           g.hasOdometer ? (long)g.odometerKm : -1L,
           doorsLocked ? 1 : 0, doorsStateKnown ? 1 : 0, reverseGearActive ? 1 : 0,
           lowBeamHold.isActive() ? 1 : 0, lampBits, (unsigned long)manualLamps.secondsLeft(millis()),
           g.obcText[0x06], g.obcText[0x04], g.obcText[0x0A]);
  webServer.send(200, "application/json", responseBuffer);
}

void handleSettingsGet() {
  int length = snprintf(responseBuffer, sizeof(responseBuffer),
           "{\"unlock\":%d,\"unlockAny\":%d,\"lowbeam\":%d,\"lbSec\":%u,\"track\":%d,\"scrollMs\":%u,\"hdr\":%d,"
           "\"duck\":%d,\"duckPct\":%u,\"welcome\":%d,\"goodbye\":%d,\"zones\":[",
           settings.unlockOnEngineOff, settings.unlockAnyLock, settings.lowBeamsAfterOff, settings.lowBeamSeconds,
           settings.trackDisplay, settings.scrollStepMs, settings.headerGauges, settings.reverseDucking,
           settings.duckPercent, settings.welcomeLights, settings.goodbyeLights);
  for (uint8_t i = 0; i < HEADER_ZONE_COUNT; i++) {
    length += snprintf(responseBuffer + length, sizeof(responseBuffer) - length, "%s\"%s\"", i ? "," : "",
                       headerGaugeName((HeaderGauge)settings.zoneGauge[i]));
  }
  snprintf(responseBuffer + length, sizeof(responseBuffer) - length, "]}");
  webServer.send(200, "application/json", responseBuffer);
}

void handleSettingsSet() {
  const char *problem = changeSetting(webServer.arg("key").c_str(), webServer.arg("value").c_str());
  if (problem[0] != '\0') {
    sendResult(false, problem);
    return;
  }
  sendResult(true, "Saved");
}

void handleDoor() {
  if (refuseWhileMoving()) return;
  if (!doorsStateKnown) {
    sendResult(false, "Lock state not known yet: lock or unlock once with the key");
    return;
  }

  bool wantLocked = webServer.arg("act") == "lock";
  if (wantLocked == doorsLocked) {
    sendResult(true, wantLocked ? "Already locked" : "Already unlocked");
    return;
  }

  const uint8_t payload[] = {GM_SET_IO_COMMAND, GM_SUB_MODULE, CENTRAL_LOCK_BUTTON_JOB, 0x01};
  bool queued = ibus.queueFrame(DIAGNOSTIC_ADDRESS, GM_ADDRESS, payload, sizeof(payload));
  sendResult(queued, queued ? (wantLocked ? "Locking" : "Unlocking") : "Bus busy, try again");
}

void handleSeat() {
  if (refuseWhileMoving()) return;

  String moveName = webServer.arg("move");
  uint8_t param = seatParamForName(moveName.c_str());
  if (param == SEAT_NO_MOVE) {
    sendResult(false, "Unknown seat move");
    return;
  }
  seatRepeater.start(param, 1, millis());
  sendResult(true, "Moving");
}

void handleLamp() {
  String id = webServer.arg("id");
  bool on = webServer.arg("on") == "1";

  if (on && refuseWhileMoving()) return;

  if (id == "all") {
    manualLamps.setAll(on, millis());
  } else {
    int8_t index = lampIndexForId(id.c_str());
    if (index < 0) {
      sendResult(false, "Unknown lamp");
      return;
    }
    manualLamps.set(index, on, millis());
  }
  lampsForceSend = true;
  sendResult(true, on ? "On" : "Off");
}

void handleStopAll() {
  seatRepeater.stop();
  manualLamps.clear();
  lampsForceSend = true;
  sendResult(true, "Stopped");
}

// ---------------------------------------------------------------------------
// Wi-Fi network: only while parked, only with a password set over serial ("wifi pass <text>")
// ---------------------------------------------------------------------------

void startWifi() {
  if (strlen(settings.wifiPassword) < WIFI_PASSWORD_MIN_LENGTH) return;

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(WIFI_SSID, settings.wifiPassword)) {
    Serial.println("WIFI: could not start the access point");
    WiFi.mode(WIFI_OFF);
    return;
  }
  webServer.begin();
  wifiUp = true;
  Serial.printf("WIFI: network '%s' is up, open http://%s\n", WIFI_SSID, WiFi.softAPIP().toString().c_str());
}

void stopWifi() {
  webServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiUp = false;
  Serial.println("WIFI: network off");
}

void webControlBegin() {
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/api/state", HTTP_GET, handleState);
  webServer.on("/api/lamps", HTTP_GET, handleLamps);
  webServer.on("/api/settings", HTTP_GET, handleSettingsGet);
  webServer.on("/api/set", HTTP_POST, handleSettingsSet);
  webServer.on("/api/door", HTTP_POST, handleDoor);
  webServer.on("/api/seat", HTTP_POST, handleSeat);
  webServer.on("/api/lamp", HTTP_POST, handleLamp);
  webServer.on("/api/stop", HTTP_POST, handleStopAll);

  if (strlen(settings.wifiPassword) < WIFI_PASSWORD_MIN_LENGTH) {
    Serial.println("WIFI: no password set, the network stays off. Type: wifi pass <8+ characters>");
  }
}

void updateWebControl() {
  uint32_t nowMs = millis();

  bool movingFast = carGauges.hasSpeedRpm && carGauges.speedKmh >= WIFI_MOVING_SPEED_KMH;
  if (!movingFast) wifiMovingSinceMs = 0;
  else if (wifiMovingSinceMs == 0) wifiMovingSinceMs = nowMs;
  bool parked = !movingFast || (nowMs - wifiMovingSinceMs < WIFI_MOVING_GRACE_MS);

  bool wanted = wifiAllowed && parked && strlen(settings.wifiPassword) >= WIFI_PASSWORD_MIN_LENGTH;
  if (wanted && !wifiUp) startWifi();
  else if (!wanted && wifiUp) stopWifi();

  if (wifiUp) webServer.handleClient();
}

// "wifi on|off" switch the network, "wifi pass <8+ characters>" sets the password, "wifi" shows the status
bool handleWifiCommand(const char *arguments) {
  while (*arguments == ' ') arguments++;

  if (strncmp(arguments, "pass ", 5) == 0) {
    const char *password = arguments + 5;
    if (strlen(password) < WIFI_PASSWORD_MIN_LENGTH || strlen(password) >= sizeof(settings.wifiPassword)) {
      Serial.println("WIFI: password must be 8-31 characters");
      return true;
    }
    strncpy(settings.wifiPassword, password, sizeof(settings.wifiPassword) - 1);
    settingsSave(settings);
    if (wifiUp) stopWifi();
    Serial.println("WIFI: password saved");
  } else if (strcmp(arguments, "on") == 0) {
    wifiAllowed = true;
  } else if (strcmp(arguments, "off") == 0) {
    wifiAllowed = false;
  }
  Serial.printf("WIFI: %s, %s, password %s\n", wifiUp ? "up" : "down", wifiAllowed ? "allowed" : "switched off",
                strlen(settings.wifiPassword) >= WIFI_PASSWORD_MIN_LENGTH ? "set" : "NOT set");
  return true;
}
