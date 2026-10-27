#pragma once

#include <stdint.h>
#include <string.h>

// Scrolls a long text through a fixed-width window (the screen shows 8 big
// characters). Short texts are sent once and do not scroll.
class TitleMarquee {
public:
  static constexpr uint8_t WINDOW_WIDTH = 8;
  static constexpr uint8_t MAX_TEXT_LENGTH = 160;
  static constexpr uint32_t EDGE_HOLD_MS = 1500;

  void setStepMs(uint32_t newStepMs) { stepMs = newStepMs; }

  void setText(const char *newText) {
    strncpy(text, newText, MAX_TEXT_LENGTH - 1);
    text[MAX_TEXT_LENGTH - 1] = '\0';
    length = (uint8_t)strlen(text);
    position = 0;
    needsSend = true;
  }

  bool isUpdateDue(uint32_t nowMs) const {
    if (length == 0) return false;
    return needsSend || (length > WINDOW_WIDTH && (int32_t)(nowMs - nextStepMs) >= 0);
  }

  // Window that commit() will make current. Does not change any state, so a
  // failed send can simply be retried.
  void windowToSend(char *out) const {
    uint8_t start = candidatePosition();
    uint8_t windowLength = length - start < WINDOW_WIDTH ? length - start : WINDOW_WIDTH;
    memcpy(out, text + start, windowLength);
    out[windowLength] = '\0';
  }

  void commit(uint32_t nowMs) {
    position = candidatePosition();
    needsSend = false;
    bool atEdge = position == 0 || position == lastPosition();
    nextStepMs = nowMs + (atEdge ? EDGE_HOLD_MS : stepMs);
  }

private:
  char text[MAX_TEXT_LENGTH] = "";
  uint8_t length = 0;
  uint8_t position = 0;
  bool needsSend = false;
  uint32_t nextStepMs = 0;
  uint32_t stepMs = 300;

  uint8_t lastPosition() const { return length > WINDOW_WIDTH ? length - WINDOW_WIDTH : 0; }

  uint8_t candidatePosition() const {
    if (needsSend) return position;
    return position < lastPosition() ? position + 1 : 0;
  }
};
