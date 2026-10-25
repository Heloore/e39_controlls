#pragma once

#include <Arduino.h>
#include "driver/uart.h"
#include "IbusParser.h"

// Half-duplex I-Bus access through a divider on RX and a 2N7000 on TX.
// RX is non-inverted, TX is inverted (the MOSFET pulls the bus low).
// Sending is queued and only happens when the bus has been idle,
// every transmitted byte is verified against its echo on RX to detect collisions.
class IbusBus {
public:
  using FrameHandler = void (*)(const uint8_t *frame, uint8_t length);

  void setRejectedFrameHandler(FrameHandler onRejectedFrame) { rejectedFrameHandler = onRejectedFrame; }

  void begin(HardwareSerial &uartSerial,
             uart_port_t uartPort,
             int rxPin,
             int txPin,
             FrameHandler onFrame);

  // Call from loop(): reads incoming bytes and sends the queued frame when possible.
  void poll();

  // Queues one frame for sending. Returns false if a frame is already pending
  // or the payload is too long.
  // logWhenSent = false keeps routine traffic (e.g. the scrolling title) off the serial port.
  bool queueFrame(uint8_t source,
                  uint8_t destination,
                  const uint8_t *payload,
                  uint8_t payloadLength,
                  bool logWhenSent = true);

  bool hasPendingFrame() const { return pendingLength > 0; }
  bool isBusIdle() const;

private:
  static constexpr uint32_t BUS_IDLE_US = 2000;
  static constexpr uint32_t ECHO_TIMEOUT_US = 5000;
  static constexpr uint8_t MAX_SEND_ATTEMPTS = 3;
  static constexpr uint32_t RETRY_BACKOFF_MIN_MS = 5;
  static constexpr uint32_t RETRY_BACKOFF_MAX_MS = 25;

  HardwareSerial *uart = nullptr;
  FrameHandler frameHandler = nullptr;
  FrameHandler rejectedFrameHandler = nullptr;
  IbusParser parser;

  uint8_t pendingFrame[IBUS_MAX_FRAME];
  uint8_t pendingLength = 0;
  uint8_t pendingAttempts = 0;
  bool pendingLogWhenSent = true;
  uint32_t nextAttemptMs = 0;

  uint32_t lastActivityUs = 0;

  void readIncoming();
  void trySendPending();
  bool transmitPendingFrame();
  bool waitForEcho(uint8_t expectedByte);
};
