#include <ESP_I2S.h>
#include "BluetoothA2DPSink.h"
#include "IbusBus.h"
#include "FrameLogger.h"
#include "LcmLights.h"
#include "GmJobScanner.h"
#include "RadioScreenText.h"
#include "TitleMarquee.h"

// ============================================================
// I2S / PCM5102A
// ============================================================

I2SClass I2S;
BluetoothA2DPSink a2dp_sink(I2S);

const int I2S_BCK = 14;
const int I2S_LCK = 15;
const int I2S_DIN = 22;

// ============================================================
// I-BUS (RX: GPIO16 divider, TX: GPIO17 -> 2N7000, inverted)
// ============================================================

#define IBUS_RX_PIN 16
#define IBUS_TX_PIN 17

IbusBus ibus;
FrameLogger frameLogger;
bool ibusTxEnabled = false; // serial "tx on" to allow sending
LcmBitScanner lcmBitScanner;
GmJobScanner gmJobScanner;

char trackTitle[64] = "";
char trackArtist[64] = "";

bool trackDisplayEnabled = true; // serial "track off" disables
bool radioShowsAux = false;
TitleMarquee trackMarquee;
volatile bool audioPlaying = false;
volatile uint32_t audioStateChangedMs = 0;
bool trackTextIsAux = false;   // what we want the screen line to say
bool ourTextOnScreen = false;  // the radio's own text has been replaced by ours
const uint32_t AUX_RESTORE_DELAY_MS = 3000; // pause this long before going back to "AUX"
volatile bool trackTextPending = false;
uint32_t trackTextDueMs = 0;
const uint32_t TRACK_TEXT_DELAY_MS = 100; // let the radio finish redrawing first

// ============================================================
// СОСТОЯНИЯ АВТОМОБИЛЯ И ГРОМКОСТЬ
// ============================================================

bool reverseGearActive = false;
uint8_t ignitionStatus = 0x00; // 0x00 off, 0x01 accessory, 0x03 on, 0x07 start
bool volumeReducedForReverse = false;
int normalVolume = 100;
int currentRampVolume = 100;

unsigned long lastVolumeStepTime = 0;

const unsigned long VOLUME_STEP_INTERVAL = 25; // мс
const int ATTENUATION_PERCENT = 30;

unsigned long lastPlayPauseTime = 0;

// Прототипы функций
void onIbusFrame(const uint8_t *frame, uint8_t length);
void onRejectedIbusFrame(const uint8_t *frame, uint8_t length);
void dispatchIbusFrame(const uint8_t *frame, uint8_t length);
void handleMflCommand(const uint8_t *frame, uint8_t length);
void handleIkeCommand(const uint8_t *frame, uint8_t length);
void handleIgnitionStatus(uint8_t newStatus);
void updateAudioVolume();
void updateVolumeRamp();
void updateLcmBitScan();
void updateGmJobScan();
void onTrackMetadata(uint8_t attributeId, const uint8_t *text);
void handleRadioCommand(const uint8_t *frame, uint8_t length);
void scheduleTrackText();
void updateTrackText();
bool handleTxTestCommand(const char *line);
bool handleRawTxCommand(const char *hexBytes);
bool handleScreenTextTestCommand(const char *text);

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=============================================");
  Serial.println(" BMW E39 Bluetooth + PCM5102A + I-BUS");
  Serial.println(" Mode: RX + safe TX (inverted, idle check)");
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
      audioPlaying = (state == ESP_A2D_AUDIO_STATE_STARTED);
      audioStateChangedMs = millis();
    }
  );

  a2dp_sink.set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST);
  a2dp_sink.set_avrc_metadata_callback(onTrackMetadata);

  a2dp_sink.set_auto_reconnect(true, 1000);
  a2dp_sink.start("E39 Bluetooth");
  Serial.println("A2DP STARTED");

  // ==========================================================
  // I-BUS UART (RX normal, TX inverted for 2N7000)
  // ==========================================================

  frameLogger.begin(Serial);
  frameLogger.setExtraCommandHandler(handleTxTestCommand);
  ibus.setRejectedFrameHandler(onRejectedIbusFrame);
  ibus.begin(Serial2, UART_NUM_2, IBUS_RX_PIN, IBUS_TX_PIN, onIbusFrame);

  Serial.println("I-BUS STARTED");
  Serial.println("I-BUS RX = GPIO16, TX = GPIO17 (inverted)");
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

  ibus.poll();
  frameLogger.pollSerial(Serial, millis());

  updateVolumeRamp();
  updateLcmBitScan();
  updateGmJobScan();
  updateTrackText();
}

// ============================================================
// УПРАВЛЕНИЕ ГРОМКОСТЬЮ (ПРИГЛУШЕНИЕ ДЛЯ REVERSE)
// ============================================================

void updateAudioVolume() {
  bool shouldReduce = reverseGearActive;

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
// I-BUS FRAME HANDLER
// ============================================================

void onRejectedIbusFrame(const uint8_t *frame, uint8_t length) {
  frameLogger.logRejectedFrame(frame, length, millis());
}

void onIbusFrame(const uint8_t *frame, uint8_t length) {
  frameLogger.logFrame(frame, length, millis());
  dispatchIbusFrame(frame, length);
}

// ============================================================
// I-BUS DISPATCHER
// ============================================================

void dispatchIbusFrame(const uint8_t *frame, uint8_t length) {
  if (length < 4) return; // Безопасность: защита от коротких кадров

  uint8_t sender = frame[0];

  switch (sender) {
    case 0x50: // MFL
      handleMflCommand(frame, length);
      break;
    case 0x80: // IKE
      handleIkeCommand(frame, length);
      break;
    case 0x68: // Radio
      handleRadioCommand(frame, length);
      break;
    default:
      break;
  }
}

// ============================================================
// MFL — КНОПКИ НА РУЛЕ
// ============================================================

void handleMflCommand(const uint8_t *frame, uint8_t length) {
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

void handleIkeCommand(const uint8_t *frame, uint8_t length) {
  if (length < 6) return;

  if (frame[2] == 0xBF && frame[3] == 0x11) {
    handleIgnitionStatus(frame[4]);
    return;
  }

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

void handleIgnitionStatus(uint8_t newStatus) {
  if (newStatus == ignitionStatus) return;

  bool justTurnedOff = ignitionStatus != 0x00 && newStatus == 0x00;
  ignitionStatus = newStatus;

  Serial.printf(">> IBUS [CAR STATE]: Ignition status -> %02X\n", ignitionStatus);
  if (justTurnedOff) {
    Serial.println(">> IBUS [CAR STATE]: Ignition OFF");
  }
}

// ============================================================
// TX TEST COMMANDS (serial): "tx on|off", "l off|park|follow|goodbye",
// "track on|off" track name on AUX screen, "screen <text>" raw text line test (any length),
// "scan 0-7" bit scan, "scan stop", "gmscan <from> <to>" (hex GM job ids), "gmscan stop",
// "r <dest> <payload...>" raw hex frame from 0x3F, e.g. "r BF 0C 00 00 00 C0 00 00 00 06"
// ============================================================

bool handleTxTestCommand(const char *line) {
  if (strcmp(line, "track on") == 0 || strcmp(line, "track off") == 0) {
    trackDisplayEnabled = strcmp(line, "track on") == 0;
    Serial.println(trackDisplayEnabled ? "TRACK DISPLAY: enabled" : "TRACK DISPLAY: disabled");
    return true;
  }

  if (strcmp(line, "tx on") == 0 || strcmp(line, "tx off") == 0) {
    ibusTxEnabled = strcmp(line, "tx on") == 0;
    Serial.println(ibusTxEnabled ? "TX: enabled" : "TX: disabled");
    return true;
  }

  if (strcmp(line, "gmscan stop") == 0) {
    gmJobScanner.stop();
    Serial.println("GMSCAN: stopped");
    return true;
  }

  if (strncmp(line, "gmscan ", 7) == 0) {
    unsigned int fromJob = 0, toJob = 0;
    if (!ibusTxEnabled || sscanf(line + 7, "%x %x", &fromJob, &toJob) != 2 || fromJob > toJob || toJob > 0xFF) {
      Serial.println("GMSCAN: needs 'tx on' and 'gmscan <from> <to>' in hex, from <= to");
      return true;
    }
    gmJobScanner.start(fromJob, toJob, millis());
    Serial.printf("GMSCAN: jobs %02X-%02X, one every %lu ms, 'gmscan stop' to abort\n",
                  fromJob, toJob, (unsigned long)GmJobScanner::STEP_MS);
    return true;
  }

  if (strncmp(line, "screen ", 7) == 0) {
    return handleScreenTextTestCommand(line + 7);
  }

  if (strcmp(line, "scan stop") == 0) {
    lcmBitScanner.stop();
    Serial.println("SCAN: stopped");
    return true;
  }

  if (strncmp(line, "scan ", 5) == 0) {
    int byteIndex = atoi(line + 5);
    if (!ibusTxEnabled || byteIndex < 0 || byteIndex > 7) {
      Serial.println("SCAN: needs 'tx on' and a byte index 0-7");
      return true;
    }
    lcmBitScanner.start(byteIndex, millis());
    Serial.printf("SCAN: byte %d, one bit every %lu ms, 'scan stop' to abort\n",
                  byteIndex, (unsigned long)LcmBitScanner::STEP_MS);
    return true;
  }

  if (line[0] == 'r' && line[1] == ' ') {
    return handleRawTxCommand(line + 2);
  }

  LcmLightMode mode;
  if (line[0] == 'l' && line[1] == ' ' && lcmLightModeFromName(line + 2, mode)) {
    if (!ibusTxEnabled) {
      Serial.println("TX: disabled, send 'tx on' first");
      return true;
    }
    const LcmLightCommand &command = lcmLightCommand(mode);
    bool queued = ibus.queueFrame(DIAGNOSTIC_ADDRESS, command.destination,
                                  command.payload, command.payloadLength);
    Serial.println(queued ? "TX: light command queued" : "TX: busy, try again");
    return true;
  }

  return false;
}

bool handleRawTxCommand(const char *hexBytes) {
  if (!ibusTxEnabled) {
    Serial.println("TX: disabled, send 'tx on' first");
    return true;
  }

  uint8_t bytes[IBUS_MAX_PAYLOAD + 1];
  uint8_t byteCount = 0;
  const char *cursor = hexBytes;

  while (*cursor != '\0' && byteCount < sizeof(bytes)) {
    char *parsedEnd;
    long value = strtol(cursor, &parsedEnd, 16);
    if (parsedEnd == cursor || value < 0 || value > 0xFF) {
      Serial.println("TX: bad hex");
      return true;
    }
    bytes[byteCount++] = (uint8_t)value;
    cursor = parsedEnd;
  }

  if (byteCount < 2) {
    Serial.println("TX: need destination and at least one payload byte");
    return true;
  }

  bool queued = ibus.queueFrame(DIAGNOSTIC_ADDRESS, bytes[0], bytes + 1, byteCount - 1);
  Serial.println(queued ? "TX: raw frame queued" : "TX: busy or too long");
  return true;
}

void updateLcmBitScan() {
  if (!lcmBitScanner.isStepDue(millis())) return;

  uint8_t payload[IBUS_MAX_PAYLOAD];
  uint8_t payloadLength = lcmBuildScanPayload(lcmBitScanner.dataByteIndex(),
                                              lcmBitScanner.bitMask(), payload);

  if (!ibus.queueFrame(DIAGNOSTIC_ADDRESS, 0xD0, payload, payloadLength)) return;

  Serial.printf("SCAN: byte %u mask %02X\n", lcmBitScanner.dataByteIndex(), lcmBitScanner.bitMask());
  lcmBitScanner.advance(millis());
}

void updateGmJobScan() {
  if (!gmJobScanner.isStepDue(millis())) return;

  const uint8_t payload[] = {0x0C, gmJobScanner.job(), 0x01};
  if (!ibus.queueFrame(DIAGNOSTIC_ADDRESS, 0x00, payload, sizeof(payload))) return;

  Serial.printf("GMSCAN: job %02X\n", gmJobScanner.job());
  gmJobScanner.advance(millis());
}

void onTrackMetadata(uint8_t attributeId, const uint8_t *text) {
  char *target = nullptr;
  size_t targetSize = 0;

  if (attributeId == ESP_AVRC_MD_ATTR_TITLE) {
    target = trackTitle;
    targetSize = sizeof(trackTitle);
  } else if (attributeId == ESP_AVRC_MD_ATTR_ARTIST) {
    target = trackArtist;
    targetSize = sizeof(trackArtist);
  } else {
    return;
  }

  strncpy(target, (const char *)text, targetSize - 1);
  target[targetSize - 1] = '\0';

  Serial.printf(">> TRACK: %s - %s\n", trackArtist, trackTitle);
  trackTextPending = true;
  trackTextDueMs = millis();
}

// ============================================================
// RADIO — ТЕКСТ НА ЭКРАНЕ (AUX -> НАЗВАНИЕ ТРЕКА)
// ============================================================

void handleRadioCommand(const uint8_t *frame, uint8_t length) {
  if (!isRadioMainTextFrame(frame, length)) return;

  radioShowsAux = isAuxMainText(frame, length);
  ourTextOnScreen = false; // the radio just drew its own text
  if (radioShowsAux) scheduleTrackText();
}

void scheduleTrackText() {
  trackTextPending = true;
  trackTextDueMs = millis() + TRACK_TEXT_DELAY_MS;
}

// Track title while music plays, the radio's "AUX" again after a pause.
void updateTrackText() {
  if (!trackDisplayEnabled || !radioShowsAux) return;

  uint32_t nowMs = millis();

  bool wantAuxText = !audioPlaying && (nowMs - audioStateChangedMs >= AUX_RESTORE_DELAY_MS);
  if (wantAuxText != trackTextIsAux && !trackTextPending) scheduleTrackText();

  if (trackTextPending && (int32_t)(nowMs - trackTextDueMs) >= 0) {
    trackTextPending = false;
    trackTextIsAux = wantAuxText;

    if (wantAuxText && !ourTextOnScreen) return; // radio's own "AUX" is already shown

    char displayText[TitleMarquee::MAX_TEXT_LENGTH];
    if (wantAuxText) {
      strcpy(displayText, "AUX");
    } else {
      formatTrackDisplayText(trackArtist, trackTitle, displayText, sizeof(displayText));
      Serial.printf(">> SCREEN TRACK: %s\n", displayText);
    }
    trackMarquee.setText(displayText);
  }

  if (!trackMarquee.isUpdateDue(nowMs)) return;

  char window[TitleMarquee::WINDOW_WIDTH + 1];
  trackMarquee.windowToSend(window);

  uint8_t payload[IBUS_MAX_PAYLOAD];
  uint8_t payloadLength = buildScreenTextPayload(window, strlen(window), payload);

  if (!ibus.queueFrame(RADIO_ADDRESS, SCREEN_ADDRESS, payload, payloadLength, false)) return;

  trackMarquee.commit(nowMs);
  ourTextOnScreen = !trackTextIsAux;
}

bool handleScreenTextTestCommand(const char *text) {
  if (!ibusTxEnabled) {
    Serial.println("TX: disabled, send 'tx on' first");
    return true;
  }

  size_t textLength = strlen(text);
  if (textLength == 0 || textLength > IBUS_MAX_PAYLOAD - 3) {
    Serial.printf("SCREEN: text must be 1-%d characters\n", IBUS_MAX_PAYLOAD - 3);
    return true;
  }

  uint8_t payload[IBUS_MAX_PAYLOAD];
  payload[0] = 0x23;
  payload[1] = 0x62;
  payload[2] = 0x10;
  memcpy(payload + 3, text, textLength);

  bool queued = ibus.queueFrame(RADIO_ADDRESS, SCREEN_ADDRESS, payload, 3 + textLength);
  Serial.println(queued ? "SCREEN: text queued" : "SCREEN: busy, try again");
  return true;
}
