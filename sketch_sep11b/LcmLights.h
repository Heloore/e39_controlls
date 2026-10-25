#pragma once

#include <stdint.h>
#include <string.h>

// Light-control frames taken from the E46 reference
// (muki01/BMW_IBus_KBus, E46_Codes.h). NOT yet verified on this E39.
// Every command is sent from the diagnostic address 0x3F.

enum class LcmLightMode { Off, ParkLights, FollowMeHome, GoodbyeLights };

struct LcmLightCommand {
  uint8_t destination;
  uint8_t payload[16];
  uint8_t payloadLength;
};

constexpr uint8_t DIAGNOSTIC_ADDRESS = 0x3F;

inline const LcmLightCommand &lcmLightCommand(LcmLightMode mode) {
  static const LcmLightCommand off = {
    0xD0, {0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xE4, 0xFF, 0x00}, 13};
  static const LcmLightCommand parkLights = {
    0xBF, {0x0C, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x06}, 9}; // left + right parking light bits
  static const LcmLightCommand followMeHome = {
    0xBF, {0x0C, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x06}, 9};
  static const LcmLightCommand goodbyeLights = {
    0xBF, {0x0C, 0x00, 0x00, 0x00, 0x00, 0x62, 0x08, 0xA0, 0x06}, 9};

  switch (mode) {
    case LcmLightMode::ParkLights:    return parkLights;
    case LcmLightMode::FollowMeHome:  return followMeHome;
    case LcmLightMode::GoodbyeLights: return goodbyeLights;
    default:                          return off;
  }
}

// Maps a serial test word to a mode. Returns false for unknown words.
inline bool lcmLightModeFromName(const char *name, LcmLightMode &mode) {
  if (strcmp(name, "off") == 0)      { mode = LcmLightMode::Off; return true; }
  if (strcmp(name, "park") == 0)     { mode = LcmLightMode::ParkLights; return true; }
  if (strcmp(name, "follow") == 0)   { mode = LcmLightMode::FollowMeHome; return true; }
  if (strcmp(name, "goodbye") == 0)  { mode = LcmLightMode::GoodbyeLights; return true; }
  return false;
}

// Builds the long "set IO" frame (to 0xD0) with a single bit set in one of the
// eight data bytes after 0x0C, to find out which lamp each bit controls.
inline uint8_t lcmBuildScanPayload(uint8_t dataByteIndex, uint8_t bitMask, uint8_t *outPayload) {
  const LcmLightCommand &base = lcmLightCommand(LcmLightMode::Off);
  memcpy(outPayload, base.payload, base.payloadLength);
  outPayload[1 + dataByteIndex] = bitMask;
  return base.payloadLength;
}

// Steps through the 8 bits of one data byte, one every STEP_MS.
class LcmBitScanner {
public:
  static constexpr uint32_t STEP_MS = 3500;

  void start(uint8_t byteIndex, uint32_t nowMs) {
    scannedByteIndex = byteIndex;
    bitIndex = 0;
    running = true;
    nextStepMs = nowMs;
  }
  void stop() { running = false; }

  bool isStepDue(uint32_t nowMs) const { return running && (int32_t)(nowMs - nextStepMs) >= 0; }
  uint8_t dataByteIndex() const { return scannedByteIndex; }
  uint8_t bitMask() const { return (uint8_t)(1 << bitIndex); }

  void advance(uint32_t nowMs) {
    nextStepMs = nowMs + STEP_MS;
    if (++bitIndex >= 8) running = false;
  }

private:
  uint8_t scannedByteIndex = 0;
  uint8_t bitIndex = 0;
  bool running = false;
  uint32_t nextStepMs = 0;
};
