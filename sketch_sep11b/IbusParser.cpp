#include "IbusParser.h"
#include <string.h>

uint8_t ibusChecksum(const uint8_t *data, uint8_t length) {
  uint8_t checksum = 0x00;
  for (uint8_t i = 0; i < length; i++) {
    checksum ^= data[i];
  }
  return checksum;
}

bool ibusChecksumValid(const uint8_t *frame, uint8_t length) {
  return ibusChecksum(frame, length) == 0x00;
}

uint8_t ibusBuildFrame(uint8_t source,
                       uint8_t destination,
                       const uint8_t *payload,
                       uint8_t payloadLength,
                       uint8_t *outFrame) {
  if (payloadLength > IBUS_MAX_PAYLOAD) return 0;

  uint8_t index = 0;
  outFrame[index++] = source;
  outFrame[index++] = payloadLength + 2; // destination + payload + checksum
  outFrame[index++] = destination;
  for (uint8_t i = 0; i < payloadLength; i++) {
    outFrame[index++] = payload[i];
  }
  outFrame[index] = ibusChecksum(outFrame, index);
  return index + 1;
}

bool IbusParser::feed(uint8_t incomingByte, uint32_t nowMs) {
  completedLength = 0;
  rejectedLength = 0;

  if (nowMs - lastByteTimeMs > IBUS_INTERBYTE_TIMEOUT_MS && bufferIndex > 0) {
    bufferIndex = 0;
  }
  lastByteTimeMs = nowMs;

  return consume(incomingByte);
}

bool IbusParser::consume(uint8_t incomingByte) {
  if (bufferIndex == 0) {
    buffer[bufferIndex++] = incomingByte;
    return false;
  }

  if (bufferIndex == 1) {
    expectedLength = incomingByte;
    if (expectedLength < 2 || expectedLength > (IBUS_MAX_FRAME - 2)) {
      bufferIndex = 0;
    } else {
      buffer[bufferIndex++] = incomingByte;
    }
    return false;
  }

  buffer[bufferIndex++] = incomingByte;

  if (bufferIndex < (2 + expectedLength)) return false;

  uint8_t length = bufferIndex;
  bufferIndex = 0;

  if (ibusChecksumValid(buffer, length)) {
    completedLength = length;
    return true;
  }

  if (rejectedLength == 0) {
    rejectedLength = length;
    memcpy(rejectedBuffer, buffer, length);
  }
  return resyncAfterRejection(length);
}

// A stray byte can make the parser swallow the start of the next real frame.
// Replay everything after the first byte so such a frame is not lost.
bool IbusParser::resyncAfterRejection(uint8_t rejectedFrameLength) {
  uint8_t leftover[IBUS_MAX_FRAME];
  memcpy(leftover, buffer + 1, rejectedFrameLength - 1);

  for (uint8_t i = 0; i < rejectedFrameLength - 1; i++) {
    if (consume(leftover[i])) return true;
  }
  return false;
}
