#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "CarGauges.h"

// Gauges written into the navigation screen header zones (radio 68 -> 3B, A5 62 01 <zone> text).
// Layout seen on this car: zone 1 top right under "P1", zone 5 top middle, zone 6 right below zone 5.
// Zone 42 is the "P1" label at the top right (the radio draws it, we overwrite it).
// The text appears immediately, no refresh frame needed.
enum class HeaderGauge { Off, Speed, Rpm, Coolant, Ambient };

struct HeaderZone {
  uint8_t zone;
  uint8_t width;        // characters the zone shows; shorter text is padded with spaces
  HeaderGauge gauge;
};

constexpr uint8_t HEADER_ZONE_COUNT = 4;
constexpr uint8_t HEADER_TEXT_MAX_LENGTH = 12;

inline bool headerGaugeFromName(const char *name, HeaderGauge &gauge) {
  if (strcmp(name, "off") == 0)     { gauge = HeaderGauge::Off; return true; }
  if (strcmp(name, "speed") == 0)   { gauge = HeaderGauge::Speed; return true; }
  if (strcmp(name, "rpm") == 0)     { gauge = HeaderGauge::Rpm; return true; }
  if (strcmp(name, "coolant") == 0) { gauge = HeaderGauge::Coolant; return true; }
  if (strcmp(name, "ambient") == 0) { gauge = HeaderGauge::Ambient; return true; }
  return false;
}

inline const char *headerGaugeName(HeaderGauge gauge) {
  switch (gauge) {
    case HeaderGauge::Speed:   return "speed";
    case HeaderGauge::Rpm:     return "rpm";
    case HeaderGauge::Coolant: return "coolant";
    case HeaderGauge::Ambient: return "ambient";
    default:                   return "off";
  }
}

// Returns false while the value has not been received yet.
inline bool formatHeaderGauge(HeaderGauge gauge, const CarGauges &gauges, uint8_t width, char *out) {
  char text[HEADER_TEXT_MAX_LENGTH] = "";
  switch (gauge) {
    case HeaderGauge::Speed:
      if (!gauges.hasSpeedRpm) return false;
      snprintf(text, sizeof(text), "%u", gauges.speedKmh);
      break;
    case HeaderGauge::Rpm:
      if (!gauges.hasSpeedRpm) return false;
      snprintf(text, sizeof(text), "%u", gauges.rpm);
      break;
    case HeaderGauge::Coolant:
      if (!gauges.hasTemperatures) return false;
      snprintf(text, sizeof(text), "%uC", gauges.coolantC);
      break;
    case HeaderGauge::Ambient:
      if (!gauges.hasTemperatures) return false;
      snprintf(text, sizeof(text), "%dC", gauges.ambientC);
      break;
    default:
      return false;
  }
  snprintf(out, HEADER_TEXT_MAX_LENGTH, "%-*.*s", width, width, text);
  return true;
}
