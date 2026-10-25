#pragma once

#include <Arduino.h>
#include "FrameSignatureSet.h"

// Timestamped I-Bus frame log with serial-driven marks and baseline filtering.
// Serial commands:
//   m <label>  insert a MARK line (e.g. "m unlock")
//   b          learn a 60 s baseline, then print only frame kinds not seen in it
//   a          print all frames
//   ?          help
class FrameLogger {
public:
  // Called for commands the logger does not know; return true if handled.
  using ExtraCommandHandler = bool (*)(const char *line);

  void begin(Print &logOutput);
  void setExtraCommandHandler(ExtraCommandHandler handler) { extraCommandHandler = handler; }
  void pollSerial(Stream &input, uint32_t nowMs);
  void logFrame(const uint8_t *frame, uint8_t length, uint32_t nowMs);
  // Frames dropped for a bad checksum; printed only in "show all" mode.
  void logRejectedFrame(const uint8_t *frame, uint8_t length, uint32_t nowMs);
  void handleCommand(const char *line, uint32_t nowMs);

private:
  enum class Mode { ShowAll, LearnBaseline, ShowNew };

  static constexpr uint32_t BASELINE_DURATION_MS = 60000;
  static constexpr uint8_t COMMAND_BUFFER_SIZE = 48;

  Print *output = nullptr;
  ExtraCommandHandler extraCommandHandler = nullptr;
  Mode mode = Mode::ShowAll;
  FrameSignatureSet baseline;
  uint32_t baselineStartMs = 0;

  char commandBuffer[COMMAND_BUFFER_SIZE];
  uint8_t commandLength = 0;

  void startBaseline(uint32_t nowMs);
  void finishBaselineIfElapsed(uint32_t nowMs);
  void printFrame(const uint8_t *frame, uint8_t length, uint32_t nowMs);
  void printHelp();
};
