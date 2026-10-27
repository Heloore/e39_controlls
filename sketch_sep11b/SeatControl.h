#pragma once

#include <stdint.h>
#include <string.h>
#include "GmJobScanner.h"

// Driver seat moves through one GM diagnostic job (K-bus, to address 00): 0C 15 <move>.
// One pulse moves the seat for about one second; send it again for a longer move.
// Moves found by testing (params 0A and above did nothing).
constexpr uint8_t SEAT_JOB = 0x15;
constexpr uint8_t SEAT_NO_MOVE = 0xFF;
constexpr uint32_t SEAT_PULSE_MS = 1100;
constexpr uint8_t SEAT_MAX_PULSES = 20;

struct SeatMove {
  const char *name;
  uint8_t param;
};

constexpr SeatMove SEAT_MOVES[] = {
  {"forward", 0x00},
  {"back", 0x01},
  {"up", 0x02},
  {"down", 0x03},
  {"tilt_up", 0x04},
  {"tilt_down", 0x05},
  {"backrest_forward", 0x06},
  {"backrest_back", 0x07},
  {"headrest_up", 0x08},
  {"headrest_down", 0x09},
};

// Returns the job parameter for a move name, SEAT_NO_MOVE when the name is unknown.
inline uint8_t seatParamForName(const char *name) {
  for (const SeatMove &move : SEAT_MOVES) {
    if (strcmp(move.name, name) == 0) return move.param;
  }
  return SEAT_NO_MOVE;
}

// Seat-type ids (low nibble 5 or 6) that are not trunk, window, sunroof or other blocked jobs.
inline bool isSeatJob(uint8_t job) {
  uint8_t lowNibble = job & 0x0F;
  return (lowNibble == 0x05 || lowNibble == 0x06) && !isDangerousGmJob(job);
}

// Sends one pulse every SEAT_PULSE_MS until the requested count is done.
class SeatPulseRepeater {
public:
  void start(uint8_t moveParam, uint8_t pulseCount, uint32_t nowMs) {
    param = moveParam;
    pulsesLeft = pulseCount > SEAT_MAX_PULSES ? SEAT_MAX_PULSES : pulseCount;
    nextPulseMs = nowMs;
  }
  void stop() { pulsesLeft = 0; }

  bool isPulseDue(uint32_t nowMs) const { return pulsesLeft > 0 && (int32_t)(nowMs - nextPulseMs) >= 0; }
  uint8_t moveParam() const { return param; }
  void markPulseSent(uint32_t nowMs) {
    pulsesLeft--;
    nextPulseMs = nowMs + SEAT_PULSE_MS;
  }

private:
  uint8_t param = 0;
  uint8_t pulsesLeft = 0;
  uint32_t nextPulseMs = 0;
};
