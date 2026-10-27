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

// Long "set IO" frame for LCM_IV (diagnostic index 0x14), layout from BlueBus:
// 0C 00 00 <blinker> 00 00 <parkL|high> <parkR|tailL> <tailR> 00 <dimmer> <loadRear> 00
// Sets one bit in the data byte after 0C (0..11, not the dimmer/load bytes 9 and 10)
// to find out which lamp each bit controls.
constexpr uint8_t LCM_IO_PAYLOAD_LENGTH = 13;
constexpr uint8_t LCM_IO_DIMMER_INDEX = 9;
constexpr uint8_t LCM_IO_LOAD_REAR_INDEX = 10;
constexpr uint8_t LCM_IO_LAST_DATA_INDEX = 11;

inline bool lcmIsScannableDataByte(uint8_t dataByteIndex) {
  return dataByteIndex <= LCM_IO_LAST_DATA_INDEX &&
         dataByteIndex != LCM_IO_DIMMER_INDEX && dataByteIndex != LCM_IO_LOAD_REAR_INDEX;
}

inline uint8_t lcmBuildScanPayload(uint8_t dataByteIndex, uint8_t bitMask,
                                   uint8_t dimmerVoltage, uint8_t loadRearVoltage, uint8_t *outPayload) {
  memset(outPayload, 0, LCM_IO_PAYLOAD_LENGTH);
  outPayload[0] = 0x0C;
  outPayload[1 + LCM_IO_DIMMER_INDEX] = dimmerVoltage;
  outPayload[1 + LCM_IO_LOAD_REAR_INDEX] = loadRearVoltage;
  outPayload[1 + dataByteIndex] = bitMask;
  return LCM_IO_PAYLOAD_LENGTH;
}

// LCM answer to "3F 03 D0 0B": D0 23 3F A0 ..., dimmer at frame[19], rear load at frame[20].
constexpr uint8_t LCM_IO_STATUS_FRAME_LENGTH = 0x23;
inline bool isLcmIoStatusFrame(const uint8_t *frame, uint8_t length) {
  return length >= 21 && frame[0] == 0xD0 && frame[1] == LCM_IO_STATUS_FRAME_LENGTH && frame[2] == DIAGNOSTIC_ADDRESS;
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
