#pragma once

#include <stdint.h>
#include <string.h>

// Lamps the LCM (light module, diagnostic index 0x14) lights through the long "set IO" job.
// Layout (payload after 0C, 12 data bytes): found by scanning this car, see the project notes.
// Dimmer and rear-load bytes (indexes 9, 10) are echoed from the LCM status reply.
constexpr uint8_t LAMP_DATA_BYTES = 12;
constexpr uint8_t LAMP_PAYLOAD_LENGTH = 13;
constexpr uint8_t LAMP_DIMMER_INDEX = 9;
constexpr uint8_t LAMP_LOAD_REAR_INDEX = 10;

struct Lamp {
  const char *id;
  const char *label;
  uint8_t dataByteIndex;
  uint8_t mask;
};

constexpr Lamp LAMPS[] = {
  {"low_left",       "Low beam, driver",         5, 0x10},
  {"low_right",      "Low beam, passenger",      5, 0x20},
  {"fog_left",       "Fog light, driver",        5, 0x04},
  {"fog_right",      "Fog light, passenger",     5, 0x40},
  {"fog_rear",       "Rear fog light",           5, 0x80},
  {"reverse_left",   "Reverse light, driver",    5, 0x08},
  {"reverse_right",  "Reverse light, passenger", 7, 0x80},
  {"turn_left",      "Turn signal, left",        2, 0x80},
  {"turn_right",     "Turn signal, right",       2, 0x40},
  {"side_right",     "Side marker, passenger",   7, 0x01},
  {"rear_right_a",   "Rear light, passenger A",  7, 0x10},
  {"rear_right_b",   "Rear light, passenger B",  2, 0x01},
  {"interior",       "Interior button lights",   2, 0x20},
};
constexpr uint8_t LAMP_COUNT = sizeof(LAMPS) / sizeof(LAMPS[0]);

inline int8_t lampIndexForId(const char *id) {
  for (uint8_t i = 0; i < LAMP_COUNT; i++) {
    if (strcmp(LAMPS[i].id, id) == 0) return i;
  }
  return -1;
}

// Builds the light job payload: 0C <12 data bytes>, ready for the LCM (D0).
inline uint8_t lcmBuildLampPayload(const uint8_t *dataBytes, uint8_t dimmerVoltage,
                                   uint8_t loadRearVoltage, uint8_t *outPayload) {
  outPayload[0] = 0x0C;
  memcpy(outPayload + 1, dataBytes, LAMP_DATA_BYTES);
  outPayload[1 + LAMP_DIMMER_INDEX] = dimmerVoltage;
  outPayload[1 + LAMP_LOAD_REAR_INDEX] = loadRearVoltage;
  return LAMP_PAYLOAD_LENGTH;
}

// Lamps switched on by hand from the web page. They turn themselves off after AUTO_OFF_MS.
class ManualLamps {
public:
  static constexpr uint32_t AUTO_OFF_MS = 120000;

  void set(uint8_t lampIndex, bool on, uint32_t nowMs) {
    if (lampIndex >= LAMP_COUNT) return;
    lampOn[lampIndex] = on;
    if (on) startedMs = nowMs;
  }
  void setAll(bool on, uint32_t nowMs) {
    for (uint8_t i = 0; i < LAMP_COUNT; i++) lampOn[i] = on;
    if (on) startedMs = nowMs;
  }
  void clear() { setAll(false, 0); }

  bool isOn(uint8_t lampIndex) const { return lampIndex < LAMP_COUNT && lampOn[lampIndex]; }
  bool any() const {
    for (uint8_t i = 0; i < LAMP_COUNT; i++) {
      if (lampOn[i]) return true;
    }
    return false;
  }
  bool isExpired(uint32_t nowMs) const { return any() && (nowMs - startedMs) >= AUTO_OFF_MS; }
  uint32_t secondsLeft(uint32_t nowMs) const {
    if (!any()) return 0;
    uint32_t elapsed = nowMs - startedMs;
    return elapsed >= AUTO_OFF_MS ? 0 : (AUTO_OFF_MS - elapsed) / 1000;
  }

  // ORs the masks of every lamp that is on into the data bytes.
  void addTo(uint8_t *dataBytes) const {
    for (uint8_t i = 0; i < LAMP_COUNT; i++) {
      if (lampOn[i]) dataBytes[LAMPS[i].dataByteIndex] |= LAMPS[i].mask;
    }
  }

private:
  bool lampOn[LAMP_COUNT] = {};
  uint32_t startedMs = 0;
};
