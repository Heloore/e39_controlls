#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Radio (0x68) -> navigation screen (0x3B) main text line, e.g. "AUX", "NO TAPE".
// Seen on the bus as: 68 12 3B 23 62 10 <text bytes> <checksum>
// The screen picks the font from the text length: up to 8 characters use the
// big font, 9 and more the small font, and at most 11 characters are visible.
constexpr uint8_t RADIO_ADDRESS = 0x68;
constexpr uint8_t SCREEN_ADDRESS = 0x3B;
constexpr uint8_t SCREEN_TEXT_HEADER_LENGTH = 3;

inline bool isRadioMainTextFrame(const uint8_t *frame, uint8_t length) {
  return length > 7 &&
         frame[0] == RADIO_ADDRESS && frame[2] == SCREEN_ADDRESS &&
         frame[3] == 0x23 && frame[4] == 0x62 && frame[5] == 0x10;
}

inline bool isAuxMainText(const uint8_t *frame, uint8_t length) {
  return isRadioMainTextFrame(frame, length) && length >= 10 &&
         frame[6] == 'A' && frame[7] == 'U' && frame[8] == 'X';
}

// Keeps printable ASCII, replaces other characters with '?' and skips UTF-8
// continuation bytes. Writes at most outSize - 1 characters plus '\0'.
inline void sanitizeScreenText(const char *source, char *out, uint8_t outSize) {
  uint8_t written = 0;
  for (const char *cursor = source; *cursor != '\0' && written < outSize - 1; cursor++) {
    uint8_t character = (uint8_t)*cursor;
    if ((character & 0xC0) == 0x80) continue;
    out[written++] = (character >= 32 && character <= 126) ? (char)character : '?';
  }
  out[written] = '\0';
}

// "Artist - Title", or whichever of the two is available.
inline void formatTrackDisplayText(const char *artist, const char *title, char *out, uint8_t outSize) {
  char combined[160];
  if (artist[0] != '\0' && title[0] != '\0') {
    snprintf(combined, sizeof(combined), "%s - %s", artist, title);
  } else {
    snprintf(combined, sizeof(combined), "%s", title[0] != '\0' ? title : artist);
  }
  sanitizeScreenText(combined, out, outSize);
}

inline uint8_t buildScreenTextPayload(const char *text, uint8_t textLength, uint8_t *outPayload) {
  outPayload[0] = 0x23;
  outPayload[1] = 0x62;
  outPayload[2] = 0x10;
  memcpy(outPayload + SCREEN_TEXT_HEADER_LENGTH, text, textLength);
  return SCREEN_TEXT_HEADER_LENGTH + textLength;
}
