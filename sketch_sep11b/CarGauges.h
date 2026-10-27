#pragma once

#include <stdint.h>
#include <math.h>
#include <string.h>

// Gauge values broadcast on the bus. Formulas from BlueBus, speed/rpm/temps seen in our logs.
//   IKE 80 -> BF 18 <speed/2> <rpm/100>     km/h = byte * 2, rpm = byte * 100
//   IKE 80 -> BF 19 <ambient> <coolant>     degrees C
//   IKE 80 -> BF 17 <km lo> <km mid> <km hi> odometer in km
//   LCM D0 -> 3F A0 ... (35-byte status)    battery frame[13], oil temp frame[23..24] (unverified)
constexpr uint8_t OBC_TEXT_FIELD_COUNT = 16;
constexpr uint8_t OBC_TEXT_MAX_LENGTH = 24;

// Last raw IKE broadcast frames we do not decode yet (to compare with the dashboard by eye).
constexpr uint8_t RAW_FRAME_SLOT_COUNT = 3;
constexpr uint8_t RAW_FRAME_MAX_LENGTH = 16;
constexpr uint8_t RAW_FRAME_COMMANDS[RAW_FRAME_SLOT_COUNT] = {0x13, 0x15, 0x17}; // sensors, vehicle config, odometer?

struct CarGauges {
  uint8_t rawFrames[RAW_FRAME_SLOT_COUNT][RAW_FRAME_MAX_LENGTH] = {};
  uint8_t rawFrameLengths[RAW_FRAME_SLOT_COUNT] = {};
  bool hasSpeedRpm = false;
  uint16_t speedKmh = 0;
  uint16_t rpm = 0;
  bool hasTemperatures = false;
  int8_t ambientC = 0;
  uint8_t coolantC = 0;
  bool hasOdometer = false;
  uint32_t odometerKm = 0;
  bool hasLcmStatus = false;
  uint8_t batteryTenthsVolt = 0;
  int16_t oilTempC = 0;
  bool hasOilTemp = false;
  char obcText[OBC_TEXT_FIELD_COUNT][OBC_TEXT_MAX_LENGTH] = {};
};

inline void gaugesFromSpeedRpmFrame(CarGauges &gauges, const uint8_t *frame) {
  gauges.speedKmh = frame[4] * 2;
  gauges.rpm = frame[5] * 100;
  gauges.hasSpeedRpm = true;
}

inline void gaugesFromTemperatureFrame(CarGauges &gauges, const uint8_t *frame) {
  gauges.ambientC = (int8_t)frame[4];
  gauges.coolantC = frame[5];
  gauges.hasTemperatures = true;
}

// IKE 80 -> BF 17 <km low> <km mid> <km high> ...: odometer in km (verified: 65 7F 06 = 425829)
inline void gaugesFromOdometerFrame(CarGauges &gauges, const uint8_t *frame) {
  gauges.odometerKm = frame[4] | ((uint32_t)frame[5] << 8) | ((uint32_t)frame[6] << 16);
  gauges.hasOdometer = true;
}

constexpr uint8_t LCM_STATUS_OIL_TEMP_HIGH_INDEX = 23;
constexpr uint8_t LCM_STATUS_OIL_TEMP_LOW_INDEX = 24;
constexpr uint8_t LCM_STATUS_BATTERY_INDEX = 13;
constexpr int OIL_TEMP_OFFSET_LCM_IV = 510;

inline void gaugesFromLcmStatusFrame(CarGauges &gauges, const uint8_t *frame) {
  gauges.batteryTenthsVolt = (uint8_t)((frame[LCM_STATUS_BATTERY_INDEX] * 100) / 136);
  gauges.hasLcmStatus = true;
  if (frame[LCM_STATUS_OIL_TEMP_HIGH_INDEX] != 0) {
    float raw = frame[LCM_STATUS_OIL_TEMP_HIGH_INDEX] * 0.00005f + frame[LCM_STATUS_OIL_TEMP_LOW_INDEX] * 0.01275f;
    gauges.oilTempC = (int16_t)(67.2529f * logf(raw) + OIL_TEMP_OFFSET_LCM_IV);
    gauges.hasOilTemp = true;
  }
}

// IKE 80 -> E7 24 <field> 00 <text...>: on-board computer text (time, average speed, ...)
inline void gaugesFromObcTextFrame(CarGauges &gauges, const uint8_t *frame, uint8_t length) {
  if (length < 8) return;
  uint8_t field = frame[4];
  if (field >= OBC_TEXT_FIELD_COUNT) return;
  uint8_t textLength = length - 7; // src, len, dst, cmd, field, 00, ..., checksum
  if (textLength >= OBC_TEXT_MAX_LENGTH) textLength = OBC_TEXT_MAX_LENGTH - 1;
  memcpy(gauges.obcText[field], frame + 6, textLength);
  gauges.obcText[field][textLength] = '\0';
}

inline void gaugesStoreRawFrame(CarGauges &gauges, const uint8_t *frame, uint8_t length) {
  if (length < 4 || length > RAW_FRAME_MAX_LENGTH) return;
  for (uint8_t slot = 0; slot < RAW_FRAME_SLOT_COUNT; slot++) {
    if (RAW_FRAME_COMMANDS[slot] == frame[3]) {
      memcpy(gauges.rawFrames[slot], frame, length);
      gauges.rawFrameLengths[slot] = length;
    }
  }
}
