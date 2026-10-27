#pragma once

#include <stdint.h>
#include <Preferences.h>
#include "HeaderGauges.h"

// Settings kept in flash, so they survive the ESP restarting on every ignition cycle.
struct Settings {
  bool unlockOnEngineOff = true;
  bool unlockAnyLock = false;        // false: only when the car locked the doors while driving
  bool lowBeamsAfterOff = true;      // follow-me-home
  uint16_t lowBeamSeconds = 60;
  bool trackDisplay = true;
  uint16_t scrollStepMs = 300;
  bool headerGauges = true;
  uint8_t zoneGauge[HEADER_ZONE_COUNT] = {
    (uint8_t)HeaderGauge::Coolant, (uint8_t)HeaderGauge::Rpm,
    (uint8_t)HeaderGauge::Speed, (uint8_t)HeaderGauge::Ambient};
  bool reverseDucking = true;
  uint8_t duckPercent = 30;
  bool welcomeLights = false;        // not implemented yet
  bool goodbyeLights = false;        // not implemented yet
  char wifiPassword[32] = "";        // empty: the access point stays off
};

constexpr uint16_t LOW_BEAM_SECONDS_MIN = 10;
constexpr uint16_t LOW_BEAM_SECONDS_MAX = 300;
constexpr uint16_t SCROLL_STEP_MIN_MS = 150;
constexpr uint16_t SCROLL_STEP_MAX_MS = 1000;
constexpr uint8_t DUCK_PERCENT_MAX = 90;

inline uint32_t clampValue(uint32_t value, uint32_t low, uint32_t high) {
  return value < low ? low : (value > high ? high : value);
}

inline void settingsLoad(Settings &s) {
  Preferences prefs;
  prefs.begin("e39", true);
  s.unlockOnEngineOff = prefs.getBool("unlock", s.unlockOnEngineOff);
  s.unlockAnyLock = prefs.getBool("unlockAny", s.unlockAnyLock);
  s.lowBeamsAfterOff = prefs.getBool("lowbeam", s.lowBeamsAfterOff);
  s.lowBeamSeconds = clampValue(prefs.getUShort("lbSec", s.lowBeamSeconds), LOW_BEAM_SECONDS_MIN, LOW_BEAM_SECONDS_MAX);
  s.trackDisplay = prefs.getBool("track", s.trackDisplay);
  s.scrollStepMs = clampValue(prefs.getUShort("scrollMs", s.scrollStepMs), SCROLL_STEP_MIN_MS, SCROLL_STEP_MAX_MS);
  s.headerGauges = prefs.getBool("hdr", s.headerGauges);
  for (uint8_t i = 0; i < HEADER_ZONE_COUNT; i++) {
    char key[6];
    snprintf(key, sizeof(key), "hz%u", i);
    s.zoneGauge[i] = prefs.getUChar(key, s.zoneGauge[i]);
  }
  s.reverseDucking = prefs.getBool("duck", s.reverseDucking);
  s.duckPercent = clampValue(prefs.getUChar("duckPct", s.duckPercent), 0, DUCK_PERCENT_MAX);
  s.welcomeLights = prefs.getBool("welcome", s.welcomeLights);
  s.goodbyeLights = prefs.getBool("goodbye", s.goodbyeLights);
  prefs.getString("wifiPass", s.wifiPassword, sizeof(s.wifiPassword));
  prefs.end();
}

inline void settingsSave(const Settings &s) {
  Preferences prefs;
  prefs.begin("e39", false);
  prefs.putBool("unlock", s.unlockOnEngineOff);
  prefs.putBool("unlockAny", s.unlockAnyLock);
  prefs.putBool("lowbeam", s.lowBeamsAfterOff);
  prefs.putUShort("lbSec", s.lowBeamSeconds);
  prefs.putBool("track", s.trackDisplay);
  prefs.putUShort("scrollMs", s.scrollStepMs);
  prefs.putBool("hdr", s.headerGauges);
  for (uint8_t i = 0; i < HEADER_ZONE_COUNT; i++) {
    char key[6];
    snprintf(key, sizeof(key), "hz%u", i);
    prefs.putUChar(key, s.zoneGauge[i]);
  }
  prefs.putBool("duck", s.reverseDucking);
  prefs.putUChar("duckPct", s.duckPercent);
  prefs.putBool("welcome", s.welcomeLights);
  prefs.putBool("goodbye", s.goodbyeLights);
  prefs.putString("wifiPass", s.wifiPassword);
  prefs.end();
}
