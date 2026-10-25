#include "IbusBus.h"

void IbusBus::begin(HardwareSerial &uartSerial,
                    uart_port_t uartPort,
                    int rxPin,
                    int txPin,
                    FrameHandler onFrame) {
  uart = &uartSerial;
  frameHandler = onFrame;

  uart->begin(9600, SERIAL_8E1, rxPin, txPin, false);
  uart_set_line_inverse(uartPort, UART_SIGNAL_TXD_INV);

  lastActivityUs = micros();
}

bool IbusBus::isBusIdle() const {
  return uart->available() == 0 && (micros() - lastActivityUs) >= BUS_IDLE_US;
}

bool IbusBus::queueFrame(uint8_t source,
                         uint8_t destination,
                         const uint8_t *payload,
                         uint8_t payloadLength,
                         bool logWhenSent) {
  if (hasPendingFrame()) return false;

  uint8_t length = ibusBuildFrame(source, destination, payload, payloadLength, pendingFrame);
  if (length == 0) return false;

  pendingLength = length;
  pendingAttempts = 0;
  pendingLogWhenSent = logWhenSent;
  nextAttemptMs = millis();
  return true;
}

void IbusBus::poll() {
  readIncoming();
  trySendPending();
}

void IbusBus::readIncoming() {
  while (uart->available() > 0) {
    uint8_t incomingByte = uart->read();
    lastActivityUs = micros();

    bool frameComplete = parser.feed(incomingByte, millis());

    if (parser.rejectedFrameLength() > 0 && rejectedFrameHandler) {
      rejectedFrameHandler(parser.rejectedFrame(), parser.rejectedFrameLength());
    }
    if (frameComplete && frameHandler) {
      frameHandler(parser.frame(), parser.frameLength());
    }
  }
}

void IbusBus::trySendPending() {
  if (!hasPendingFrame()) return;
  if ((int32_t)(millis() - nextAttemptMs) < 0) return;
  if (!isBusIdle()) return;

  if (transmitPendingFrame()) {
    pendingLength = 0;
    return;
  }

  pendingAttempts++;
  if (pendingAttempts >= MAX_SEND_ATTEMPTS) {
    Serial.println(">> IBUS [TX]: dropped frame after collisions");
    pendingLength = 0;
    return;
  }

  nextAttemptMs = millis() + random(RETRY_BACKOFF_MIN_MS, RETRY_BACKOFF_MAX_MS);
}

bool IbusBus::transmitPendingFrame() {
  for (uint8_t i = 0; i < pendingLength; i++) {
    uart->write(pendingFrame[i]);
    if (!waitForEcho(pendingFrame[i])) {
      Serial.println(">> IBUS [TX]: collision detected");
      return false;
    }
  }
  lastActivityUs = micros();

  if (pendingLogWhenSent) {
    Serial.print("IBUS TX:");
    for (uint8_t i = 0; i < pendingLength; i++) {
      Serial.printf(" %02X", pendingFrame[i]);
    }
    Serial.println();
  }
  return true;
}

bool IbusBus::waitForEcho(uint8_t expectedByte) {
  uint32_t startUs = micros();

  while (micros() - startUs < ECHO_TIMEOUT_US) {
    if (uart->available() > 0) {
      return uart->read() == expectedByte;
    }
  }
  return false;
}
