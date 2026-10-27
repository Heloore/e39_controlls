#include "FrameLogger.h"

void FrameLogger::begin(Print &logOutput) {
  output = &logOutput;
}

void FrameLogger::pollSerial(Stream &input, uint32_t nowMs) {
  finishBaselineIfElapsed(nowMs);

  while (input.available() > 0) {
    char incomingChar = input.read();

    if (incomingChar == '\r') continue;

    if (incomingChar == '\n') {
      commandBuffer[commandLength] = '\0';
      handleCommand(commandBuffer, nowMs);
      commandLength = 0;
    } else if (commandLength < COMMAND_BUFFER_SIZE - 1) {
      commandBuffer[commandLength++] = incomingChar;
    }
  }
}

void FrameLogger::handleCommand(const char *line, uint32_t nowMs) {
  if (line[0] == 'm' && (line[1] == ' ' || line[1] == '\0')) {
    const char *label = line[1] == ' ' ? line + 2 : "";
    output->printf("[%lu] MARK %s\n", (unsigned long)nowMs, label);
  } else if (line[0] == 'b' && line[1] == '\0') {
    startBaseline(nowMs);
  } else if (line[0] == 'a' && line[1] == '\0') {
    mode = Mode::ShowAll;
    output->println("LOG: showing all frames");
  } else if (line[0] == 'q' && line[1] == '\0') {
    mode = Mode::Quiet;
    output->println("LOG: quiet, frames hidden ('a' shows them again)");
  } else if (extraCommandHandler && extraCommandHandler(line)) {
    return;
  } else {
    printHelp();
  }
}

void FrameLogger::logFrame(const uint8_t *frame, uint8_t length, uint32_t nowMs, const char *busLabel) {
  finishBaselineIfElapsed(nowMs);

  // Same frame kind on the other bus must not count as already seen.
  uint32_t signature = FrameSignatureSet::signatureOf(frame, length) | ((uint32_t)busLabel[0] << 24);

  switch (mode) {
    case Mode::ShowAll:
      printFrame(frame, length, nowMs, busLabel);
      break;

    case Mode::LearnBaseline:
      baseline.add(signature);
      break;

    case Mode::Quiet:
      break;

    case Mode::ShowNew:
      if (!baseline.contains(signature)) {
        printFrame(frame, length, nowMs, busLabel);
      }
      break;
  }
}

void FrameLogger::logRejectedFrame(const uint8_t *frame, uint8_t length, uint32_t nowMs, const char *busLabel) {
  if (mode != Mode::ShowAll) return;

  output->printf("[%lu] %s BAD:", (unsigned long)nowMs, busLabel);
  for (uint8_t i = 0; i < length; i++) {
    output->printf(" %02X", frame[i]);
  }
  output->println();
}

void FrameLogger::startBaseline(uint32_t nowMs) {
  baseline.clear();
  baselineStartMs = nowMs;
  mode = Mode::LearnBaseline;
  output->println("LOG: learning baseline for 60 s, do not touch anything");
}

void FrameLogger::finishBaselineIfElapsed(uint32_t nowMs) {
  if (mode != Mode::LearnBaseline) return;
  if (nowMs - baselineStartMs < BASELINE_DURATION_MS) return;

  mode = Mode::ShowNew;
  output->printf("LOG: baseline done, %u frame kinds learned, printing only new kinds\n",
                 baseline.size());
}

void FrameLogger::printFrame(const uint8_t *frame, uint8_t length, uint32_t nowMs, const char *busLabel) {
  output->printf("[%lu] %s RX:", (unsigned long)nowMs, busLabel);
  for (uint8_t i = 0; i < length; i++) {
    output->printf(" %02X", frame[i]);
  }
  output->println();
}

void FrameLogger::printHelp() {
  output->println("LOG commands: 'm <label>' mark, 'b' learn baseline 60 s, 'a' show all, 'q' quiet");
}
