#pragma once

#include <stdint.h>

// GM5 central lock "button" diagnostic job (toggles locked/unlocked, like the console button).
// Payload layout: 0C <sub-module 00> <job> <on>
constexpr uint8_t GM_ADDRESS = 0x00;
constexpr uint8_t GM_DOOR_STATUS_COMMAND = 0x7A;
constexpr uint8_t GM_SET_IO_COMMAND = 0x0C;
constexpr uint8_t GM_SUB_MODULE = 0x00;
constexpr uint8_t CENTRAL_LOCK_BUTTON_JOB = 0x14; // toggles all doors, like the console button
// BlueBus GM5 jobs: 40 lock high, 41 lock low, 42 unlock high, 43 unlock low

constexpr uint8_t DOOR_LOCK_STATE_MASK = 0x30;
constexpr uint8_t DOORS_LOCKED_BITS = 0x20;

inline bool areDoorsLocked(uint8_t doorStatusByte) {
  return (doorStatusByte & DOOR_LOCK_STATE_MASK) == DOORS_LOCKED_BITS;
}
