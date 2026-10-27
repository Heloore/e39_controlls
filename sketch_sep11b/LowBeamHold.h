#pragma once

#include <stdint.h>

// Keeps the low beams on after ignition off. The LCM drops a diagnostic light job after a
// few seconds, so it is sent again every REFRESH_MS until DURATION_MS has passed.
// Low beam bits found by scanning this car: data byte 5, bit 4 = driver, bit 5 = passenger.
constexpr uint8_t LOW_BEAM_DATA_BYTE_INDEX = 5;
constexpr uint8_t LOW_BEAM_BOTH_MASK = 0x30;
constexpr uint8_t LCM_IO_STATUS_REQUEST[] = {0x0B};
constexpr uint8_t DIAGNOSTIC_TERMINATE[] = {0x9F};

class LowBeamHold {
public:
  static constexpr uint32_t DURATION_MS = 60000;
  static constexpr uint32_t REFRESH_MS = 3000;
  static constexpr uint32_t FIRST_SEND_DELAY_MS = 800; // let the LCM status reply arrive first

  void start(uint32_t nowMs) {
    active = true;
    startedMs = nowMs;
    nextRefreshMs = nowMs + FIRST_SEND_DELAY_MS;
    statusRequested = false;
  }
  void stop() { active = false; }

  bool isActive() const { return active; }
  bool needsStatusRequest() const { return active && !statusRequested; }
  void markStatusRequested() { statusRequested = true; }
  bool isExpired(uint32_t nowMs) const { return active && (nowMs - startedMs) >= DURATION_MS; }
  bool isRefreshDue(uint32_t nowMs) const { return active && (int32_t)(nowMs - nextRefreshMs) >= 0; }
  void markRefreshed(uint32_t nowMs) { nextRefreshMs = nowMs + REFRESH_MS; }

private:
  bool active = false;
  bool statusRequested = false;
  uint32_t startedMs = 0;
  uint32_t nextRefreshMs = 0;
};
