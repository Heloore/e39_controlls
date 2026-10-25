#pragma once

#include <stdint.h>

constexpr uint8_t IBUS_MAX_FRAME = 32;
constexpr uint8_t IBUS_MAX_PAYLOAD = IBUS_MAX_FRAME - 4;
constexpr uint32_t IBUS_INTERBYTE_TIMEOUT_MS = 50;

uint8_t ibusChecksum(const uint8_t *data, uint8_t length);
bool ibusChecksumValid(const uint8_t *frame, uint8_t length);

// Frame layout: [source][length][destination][payload...][checksum]
// Returns total frame length, or 0 if the payload does not fit.
uint8_t ibusBuildFrame(uint8_t source,
                       uint8_t destination,
                       const uint8_t *payload,
                       uint8_t payloadLength,
                       uint8_t *outFrame);

class IbusParser {
public:
  // Returns true when a complete, checksum-valid frame is available.
  bool feed(uint8_t incomingByte, uint32_t nowMs);

  const uint8_t *frame() const { return buffer; }
  uint8_t frameLength() const { return completedLength; }

  // Frame just dropped for a bad checksum (length 0 if none).
  const uint8_t *rejectedFrame() const { return rejectedBuffer; }
  uint8_t rejectedFrameLength() const { return rejectedLength; }

private:
  uint8_t buffer[IBUS_MAX_FRAME];
  uint8_t bufferIndex = 0;
  uint8_t expectedLength = 0;
  uint8_t completedLength = 0;
  uint8_t rejectedBuffer[IBUS_MAX_FRAME];
  uint8_t rejectedLength = 0;
  uint32_t lastByteTimeMs = 0;

  bool consume(uint8_t incomingByte);
  bool resyncAfterRejection(uint8_t rejectedFrameLength);
};
