#pragma once

#include <stdint.h>

// Remembers which kinds of frames were seen: (source, destination, command byte).
// Payload values are ignored, so periodic frames with changing data count as one kind.
class FrameSignatureSet {
public:
  static constexpr uint8_t CAPACITY = 128;

  static uint32_t signatureOf(const uint8_t *frame, uint8_t length) {
    uint8_t commandByte = length >= 5 ? frame[3] : 0x00;
    return ((uint32_t)frame[0] << 16) | ((uint32_t)frame[2] << 8) | commandByte;
  }

  void clear() { count = 0; }

  bool contains(uint32_t signature) const {
    for (uint8_t i = 0; i < count; i++) {
      if (signatures[i] == signature) return true;
    }
    return false;
  }

  // Returns false if the set is full and the signature was not stored.
  bool add(uint32_t signature) {
    if (contains(signature)) return true;
    if (count >= CAPACITY) return false;
    signatures[count++] = signature;
    return true;
  }

  uint8_t size() const { return count; }

private:
  uint32_t signatures[CAPACITY];
  uint8_t count = 0;
};
