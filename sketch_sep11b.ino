#include <ESP_I2S.h>
#include "BluetoothA2DPSink.h"

// ============================================================
// I2S / PCM5102A
// ============================================================

I2SClass I2S;
BluetoothA2DPSink a2dp_sink(I2S);

const int I2S_BCK = 14;
const int I2S_LCK = 15;
const int I2S_DIN = 22;

// ============================================================
// I-BUS (LISTEN-ONLY MODE)
// ============================================================

#define IBUS_RX_PIN 16
#define IBUS_TX_PIN 17 // Пин подключен физически, но будет отключен программно

#define MAX_IBUS_FRAME 32

uint8_t ibusBuffer[MAX_IBUS_FRAME];
uint8_t bufferIndex = 0;
uint8_t expectedLength = 0;

unsigned long lastByteTime = 0;

// ============================================================
// СОСТОЯНИЯ АВТОМОБИЛЯ И ГРОМКОСТЬ
// ============================================================

bool reverseGearActive = false;
bool pdcActive = false;
bool volumeReducedForReverse = false;
int normalVolume = 100;
int currentRampVolume = 100;

unsigned long lastVolumeStepTime = 0;

const unsigned long VOLUME_STEP_INTERVAL = 25; // мс
const int ATTENUATION_PERCENT = 30;

unsigned long lastPlayPauseTime = 0;
unsigned long lastPdcMessageTime = 0;

const unsigned long PDC_SILENCE_TIMEOUT = 2000;

// Прототипы функций
void processIbusByte(uint8_t b);
bool validateChecksum(uint8_t *frame, uint8_t length);
void dispatchIbusFrame(uint8_t *frame, uint8_t length);
void handleMflCommand(uint8_t *frame, uint8_t length);
void handleIkeCommand(uint8_t *frame, uint8_t length);
void handlePdcCommand(uint8_t *frame, uint8_t length);
void updateAudioVolume();
void updateVolumeRamp();

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=============================================");
  Serial.println(" BMW E39 Bluetooth + PCM5102A + I-BUS");
  Serial.println(" Mode: LISTEN ONLY (TX Completely Disabled)");
  Serial.println("=============================================");

  // ==========================================================
  // I2S
  // ==========================================================

  I2S.setPins(I2S_BCK, I2S_LCK, I2S_DIN);

  if (!I2S.begin(
        I2S_MODE_STD,
        44100,
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_STEREO,
        I2S_STD_SLOT_BOTH
      )) {
    Serial.println("ERROR: I2S INIT FAILED!");
    while (true) { delay(1000); }
  }

  Serial.println("I2S OK");

  // ==========================================================
  // BLUETOOTH A2DP
  // ==========================================================

  a2dp_sink.set_on_connection_state_changed(
    [](esp_a2d_connection_state_t state, void *) {
      Serial.print("BT connection state: ");
      Serial.println((int)state);
    }
  );

  a2dp_sink.set_on_audio_state_changed(
    [](esp_a2d_audio_state_t state, void *) {
      Serial.print("AUDIO state: ");
      Serial.println((int)state);
    }
  );

  a2dp_sink.set_auto_reconnect(true, 1000);
  a2dp_sink.start("E39 Bluetooth");
  Serial.println("A2DP STARTED");

  // ==========================================================
  // I-BUS UART (ПРОГРАММНОЕ ОТКЛЮЧЕНИЕ TX)
  // ==========================================================

  // 1. Указываем -1 вместо IBUS_TX_PIN. UART модуль ESP32 не трогает TX.
  Serial2.begin(
    9600,
    SERIAL_8E1,
    IBUS_RX_PIN,
    -1,
    false
  );

  // 2. Переводим GPIO17 в высокоимпедансный вход (Hi-Z), чтобы он "висел в воздухе"
  pinMode(IBUS_TX_PIN, INPUT);

  Serial.println("I-BUS STARTED (LISTEN-ONLY)");
  Serial.println("I-BUS RX = GPIO16 (Active)");
  Serial.println("I-BUS TX = GPIO17 (Disabled in Software / Hi-Z)");
  Serial.println();
  Serial.println("System ready.");
}

// ============================================================
// LOOP
// ============================================================

void loop() {

  // ==========================================================
  // ЧИТАЕМ I-BUS
  // ==========================================================

  while (Serial2.available() > 0) {
    uint8_t incomingByte = Serial2.read();
    processIbusByte(incomingByte);
  }

  // ==========================================================
  // PDC TIMEOUT
  // ==========================================================

  if (pdcActive && (millis() - lastPdcMessageTime > PDC_SILENCE_TIMEOUT)) {
    pdcActive = false;
    Serial.println(">> IBUS [CAR STATE]: PDC -> ВЫКЛЮЧЕНЫ (PDC OFF)");
    updateAudioVolume();
  }

  updateVolumeRamp();
}

// ============================================================
// УПРАВЛЕНИЕ ГРОМКОСТЬЮ (ПРИГЛУШЕНИЕ ДЛЯ PDC / REVERSE)
// ============================================================

void updateAudioVolume() {
  bool shouldReduce = reverseGearActive || pdcActive;

  if (shouldReduce && !volumeReducedForReverse) {
    if (a2dp_sink.is_connected()) {
      int currentVol = a2dp_sink.get_volume();

      if (currentVol > 10) {
        normalVolume = currentVol;
      }

      currentRampVolume = currentVol;
      volumeReducedForReverse = true;

      Serial.println(">> AUDIO: Плавное приглушение на 30%");
    }
  }
  else if (!shouldReduce && volumeReducedForReverse) {
    volumeReducedForReverse = false;
    Serial.println(">> AUDIO: Плавное восстановление громкости");
  }
}

void updateVolumeRamp() {
  if (!a2dp_sink.is_connected()) return;

  if (millis() - lastVolumeStepTime < VOLUME_STEP_INTERVAL) {
    return;
  }

  lastVolumeStepTime = millis();

  int targetVolume = volumeReducedForReverse
      ? normalVolume * (100 - ATTENUATION_PERCENT) / 100
      : normalVolume;

  if (currentRampVolume < targetVolume) {
    currentRampVolume++;
  }
  else if (currentRampVolume > targetVolume) {
    currentRampVolume--;
  }
  else {
    return;
  }

  a2dp_sink.set_volume(currentRampVolume);
}

// ============================================================
// I-BUS PARSER
// ============================================================

void processIbusByte(uint8_t b) {

  if (millis() - lastByteTime > 50 && bufferIndex > 0) {
    bufferIndex = 0;
  }
  lastByteTime = millis();

  if (bufferIndex == 0) {
    ibusBuffer[bufferIndex++] = b;
  }
  else if (bufferIndex == 1) {
    expectedLength = b;
    if (expectedLength < 2 || expectedLength > (MAX_IBUS_FRAME - 2)) {
      bufferIndex = 0;
    } else {
      ibusBuffer[bufferIndex++] = b;
    }
  }
  else {
    ibusBuffer[bufferIndex++] = b;

    if (bufferIndex == (2 + expectedLength)) {
      if (validateChecksum(ibusBuffer, bufferIndex)) {
        dispatchIbusFrame(ibusBuffer, bufferIndex);
      }
      bufferIndex = 0;
    }
  }
}

// ============================================================
// CHECKSUM
// ============================================================

bool validateChecksum(uint8_t *frame, uint8_t length) {
  uint8_t checksum = 0x00;
  for (uint8_t i = 0; i < length; i++) {
    checksum ^= frame[i];
  }
  return checksum == 0x00;
}

// ============================================================
// I-BUS DISPATCHER
// ============================================================

void dispatchIbusFrame(uint8_t *frame, uint8_t length) {
  if (length < 4) return; // Безопасность: защита от коротких кадров

  uint8_t sender = frame[0];

  switch (sender) {
    case 0x50: // MFL
      handleMflCommand(frame, length);
      break;
    case 0x80: // IKE
      handleIkeCommand(frame, length);
      break;
    case 0x60: // PDC
      handlePdcCommand(frame, length);
      break;
    default:
      break;
  }
}

// ============================================================
// MFL — КНОПКИ НА РУЛЕ
// ============================================================

void handleMflCommand(uint8_t *frame, uint8_t length) {
  if (length < 5) return;

  if (frame[3] == 0x3B) {
    uint8_t buttonCode = frame[4];

    if (!a2dp_sink.is_connected()) {
      return;
    }

    switch (buttonCode) {
      case 0x01:
        Serial.println(">> IBUS [BT ACTION]: Next Track");
        a2dp_sink.next();
        break;

      case 0x08:
        Serial.println(">> IBUS [BT ACTION]: Previous Track");
        a2dp_sink.previous();
        break;

      case 0x80:
        if (millis() - lastPlayPauseTime < 500) return;
        lastPlayPauseTime = millis();

        if (a2dp_sink.get_audio_state() == ESP_A2D_AUDIO_STATE_STARTED) {
          Serial.println(">> IBUS [BT ACTION]: Play/Pause -> PAUSE");
          a2dp_sink.pause();
        } else {
          Serial.println(">> IBUS [BT ACTION]: Play/Pause -> PLAY");
          a2dp_sink.play();
        }
        break;

      default:
        break;
    }
  }
}

// ============================================================
// IKE — ЗАДНЯЯ ПЕРЕДАЧА
// ============================================================

void handleIkeCommand(uint8_t *frame, uint8_t length) {
  if (length < 6) return;

  if (frame[2] == 0xBF && frame[3] == 0x13 && frame[4] == 0x03) {
    bool newState = (frame[5] == 0x10);

    if (newState != reverseGearActive) {
      reverseGearActive = newState;
      Serial.print(">> IBUS [CAR STATE]: Задняя передача -> ");
      Serial.println(reverseGearActive ? "ВКЛЮЧЕНА" : "ВЫКЛЮЧЕНА");
      updateAudioVolume();
    }
  }
}

// ============================================================
// PDC — ПАРКТРОНИКИ
// ============================================================

void handlePdcCommand(uint8_t *frame, uint8_t length) {
  lastPdcMessageTime = millis();

  if (!pdcActive) {
    pdcActive = true;
    Serial.println(">> IBUS [CAR STATE]: PDC -> АКТИВНЫ");
    updateAudioVolume();
  }
}