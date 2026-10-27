#include <ESP_I2S.h>
#include "BluetoothA2DPSink.h"
#include "IbusBus.h"
#include "FrameLogger.h"
#include "LcmLights.h"
#include "GmJobScanner.h"
#include "SeatControl.h"
#include "CentralLocking.h"
#include "LowBeamHold.h"
#include "CarGauges.h"
#include "HeaderGauges.h"
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

// K-bus (body bus, wire taken at the heater module): receive only for now
#define KBUS_RX_PIN 18
#define KBUS_TX_PIN 19

IbusBus ibus;
IbusBus kbus;
FrameLogger frameLogger;
bool ibusTxEnabled = false; // serial "tx on" to allow sending
LcmBitScanner lcmBitScanner;
uint8_t lcmDimmerVoltage = 0x00;    // from the LCM status reply (r D0 0B)
uint8_t lcmLoadRearVoltage = 0x00;
GmJobScanner gmJobScanner;
uint8_t gmScanReplies[256]; // 0 = no reply
bool gmScanSent[256];
uint8_t lastScannedJob = 0;
uint32_t lastGmScanStepMs = 0;
bool gmScanSummaryPending = false;
int16_t gmScanFixedJob = -1; // >= 0: the scanner steps through the parameter of this job
IbusBus *gmScanBus = &ibus; // "gmscan" uses the I-bus, "kgmscan" the K-bus

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
bool doorsLocked = false; // from the GM door status frame
bool doorsLockedByCar = false; // locked while the ignition was on (auto-lock), not by the fob
bool unlockOnIgnitionOff = true;
uint8_t unlockJob = CENTRAL_LOCK_BUTTON_JOB; // serial "unlockjob <hex>" to try others
bool lowBeamAfterIgnitionOff = true;
LowBeamHold lowBeamHold;
CarGauges carGauges;
SeatPulseRepeater seatRepeater;
bool headerGaugesEnabled = false;  // serial "hdr on"
HeaderZone headerZones[HEADER_ZONE_COUNT] = {
  {1, 3, HeaderGauge::Speed},
  {5, 6, HeaderGauge::Coolant},
  {6, 6, HeaderGauge::Ambient},
};
char headerZoneLastText[HEADER_ZONE_COUNT][HEADER_TEXT_MAX_LENGTH] = {};
uint32_t headerZoneLastSentMs[HEADER_ZONE_COUNT] = {};
uint8_t obcScanNextField = 0;   // 0 = idle
uint32_t obcScanNextMs = 0;
bool carStreamEnabled = false;
uint32_t lastCarStreamMs = 0;
bool lowBeamTerminatePending = false;
constexpr uint32_t HEADER_REFRESH_MS = 5000;     // re-send unchanged header text this often
constexpr uint32_t HEADER_MIN_INTERVAL_MS = 1000;  // at most one update per zone per second
constexpr uint8_t IGNITION_ACC = 0x01;
constexpr uint8_t IGNITION_ON = 0x03;
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
void onKbusFrame(const uint8_t *frame, uint8_t length);
void onRejectedKbusFrame(const uint8_t *frame, uint8_t length);
void dispatchIbusFrame(const uint8_t *frame, uint8_t length);
void handleMflCommand(const uint8_t *frame, uint8_t length);
void handleIkeCommand(const uint8_t *frame, uint8_t length);
void handleGmCommand(const uint8_t *frame, uint8_t length);
void handleScreenOrButtonCommand(const uint8_t *frame, uint8_t length);
void handleIgnitionStatus(uint8_t newStatus);
void updateAudioVolume();
void updateVolumeRamp();
void updateLcmBitScan();
void updateGmJobScan();
void updateLowBeamHold();
void printCarGauges();
void updateCarStream();
void updateSeatMove();
void requestObcField(uint8_t field);
void updateObcScan();
void updateHeaderGauges();
bool handleHeaderGaugeCommand(const char *arguments);
void stopLowBeamHold();
void recordGmReply(uint8_t replyCode);
void printGmScanSummary();
void onTrackMetadata(uint8_t attributeId, const uint8_t *text);
void handleRadioCommand(const uint8_t *frame, uint8_t length);
void scheduleTrackText();
void updateTrackText();
bool handleTxTestCommand(const char *line);
bool handleRawTxCommand(const char *hexBytes, IbusBus &bus);
bool handleScreenTextTestCommand(const char *text);
bool handleSeatCommand(const char *arguments);
bool handleGtTextCommand(const char *arguments);

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

  kbus.setName("KBUS");
  kbus.setRejectedFrameHandler(onRejectedKbusFrame);
  kbus.begin(Serial1, UART_NUM_1, KBUS_RX_PIN, KBUS_TX_PIN, onKbusFrame);

  Serial.println("I-BUS STARTED");
  Serial.println("I-BUS RX = GPIO16, TX = GPIO17 (inverted)");
  Serial.println("K-BUS RX = GPIO18 (receive only), TX = GPIO19 unused");
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
  kbus.poll();
  frameLogger.pollSerial(Serial, millis());

  updateVolumeRamp();
  updateLcmBitScan();
  updateGmJobScan();
  updateLowBeamHold();
  updateCarStream();
  updateSeatMove();
  updateObcScan();
  updateHeaderGauges();
  updateTrackText();
}

// ============================================================
// УПРАВЛЕНИЕ ГРОМКОСТЬЮ (ПРИГЛУШЕНИЕ ДЛЯ REVERSE)
// ============================================================

void updateAudioVolume() {
  bool shouldReduce = reverseGearActive;
  Serial.printf(">> AUDIO: reverse=%d connected=%d volume=%d reduced=%d\n", shouldReduce,
                a2dp_sink.is_connected(), a2dp_sink.get_volume(), volumeReducedForReverse);

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

void onKbusFrame(const uint8_t *frame, uint8_t length) {
  frameLogger.logFrame(frame, length, millis(), "KBUS");

  bool isGmReplyToDiagnostics = length == 5 && frame[0] == 0x00 && frame[2] == DIAGNOSTIC_ADDRESS;
  if (isGmReplyToDiagnostics) {
    recordGmReply(frame[3]);
  }
}

void onRejectedKbusFrame(const uint8_t *frame, uint8_t length) {
  frameLogger.logRejectedFrame(frame, length, millis(), "KBUS");
}

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
    case 0x3B: // navigation screen
    case 0xF0: // BMBT buttons
      handleScreenOrButtonCommand(frame, length);
      break;
    case 0xD0: // LCM
      if (isLcmIoStatusFrame(frame, length)) {
        lcmDimmerVoltage = frame[19];
        lcmLoadRearVoltage = frame[20];
        gaugesFromLcmStatusFrame(carGauges, frame);
        Serial.printf(">> LCM: status dimmer=%02X loadRear=%02X\n", lcmDimmerVoltage, lcmLoadRearVoltage);
      }
      break;
    case 0x00: // GM
      handleGmCommand(frame, length);
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

  if (frame[2] == 0xBF) {
    gaugesStoreRawFrame(carGauges, frame, length);
  }

  if (frame[2] == 0xBF && frame[3] == 0x18) {
    gaugesFromSpeedRpmFrame(carGauges, frame);
    return;
  }

  if (frame[2] == 0xBF && frame[3] == 0x17 && length >= 8) {
    gaugesFromOdometerFrame(carGauges, frame);
    return;
  }

  if (frame[2] == 0xBF && frame[3] == 0x19) {
    gaugesFromTemperatureFrame(carGauges, frame);
    return;
  }

  if (frame[2] == 0xE7 && frame[3] == 0x24) {
    gaugesFromObcTextFrame(carGauges, frame, length);
    return;
  }

  if (frame[2] == 0xBF && frame[3] == 0x13) {
    bool newState = (frame[5] == 0x10);
    Serial.printf(">> IBUS [REVERSE]: frame 13 %02X %02X -> %s (was %s)\n", frame[4], frame[5],
                  newState ? "ON" : "OFF", reverseGearActive ? "ON" : "OFF");

    if (newState != reverseGearActive) {
      reverseGearActive = newState;
      Serial.print(">> IBUS [CAR STATE]: Задняя передача -> ");
      Serial.println(reverseGearActive ? "ВКЛЮЧЕНА" : "ВЫКЛЮЧЕНА");
      updateAudioVolume();
    }
  }
}

// MENU press (F0 -> FF 48 34) or a screen mode with bit 0x80 (3B -> 68 45 91) means the GT shows its own
// menu, so our title text has to stop until the radio draws its AUX title again.
void handleScreenOrButtonCommand(const uint8_t *frame, uint8_t length) {
  if (length < 5) return;

  bool menuButtonPressed = frame[0] == 0xF0 && frame[3] == 0x48 && frame[4] == 0x34;
  bool screenShowsMenu = frame[0] == 0x3B && frame[2] == RADIO_ADDRESS && frame[3] == 0x45 && (frame[4] & 0x80) != 0;
  if (!menuButtonPressed && !screenShowsMenu) return;

  radioShowsAux = false;
  ourTextOnScreen = false;
  trackTextPending = false;
}

void handleGmCommand(const uint8_t *frame, uint8_t length) {
  if (length < 6 || frame[2] != 0xBF || frame[3] != GM_DOOR_STATUS_COMMAND) return;
  bool wasLocked = doorsLocked;
  doorsLocked = areDoorsLocked(frame[4]);
  if (!doorsLocked) {
    doorsLockedByCar = false;
  } else if (!wasLocked) {
    doorsLockedByCar = ignitionStatus >= IGNITION_ON;
  }
}

void handleIgnitionStatus(uint8_t newStatus) {
  if (newStatus == ignitionStatus) return;

  bool justTurnedOff = ignitionStatus != 0x00 && newStatus == 0x00;
  bool engineJustStopped = ignitionStatus >= IGNITION_ON && newStatus <= IGNITION_ACC;
  ignitionStatus = newStatus;

  Serial.printf(">> IBUS [CAR STATE]: Ignition status -> %02X\n", ignitionStatus);
  if (newStatus != 0x00 && lowBeamHold.isActive()) {
    stopLowBeamHold();
  }
  if (engineJustStopped && unlockOnIgnitionOff && doorsLocked && doorsLockedByCar) {
    const uint8_t unlockPayload[] = {GM_SET_IO_COMMAND, GM_SUB_MODULE, unlockJob, 0x01};
    bool queued = ibus.queueFrame(DIAGNOSTIC_ADDRESS, GM_ADDRESS, unlockPayload, sizeof(unlockPayload));
    Serial.println(queued ? ">> LOCKS: unlocking, car locked the doors and the ignition left ON" : ">> LOCKS: unlock not queued, bus busy");
    if (queued) doorsLockedByCar = false;
  }
  if (justTurnedOff) {
    Serial.println(">> IBUS [CAR STATE]: Ignition OFF");
    if (lowBeamAfterIgnitionOff) {
      lowBeamHold.start(millis());
      Serial.println(">> LIGHTS: low beams hold started");
    }
  }
}

// ============================================================
// TX TEST COMMANDS (serial): "tx on|off", "l off|park|follow|goodbye",
// "obc scan" / "obc <field>" ask the cluster for computer texts, "car" / "car on|off" gauges (speed, rpm, temps, battery, OBC texts), "seat <move>" / "seat job <hex>" driver seat on the K-bus, "track on|off" track name on AUX screen, "screen <text>" raw text line test (any length),
// "scan 0-11" LCM bit scan (not 9, 10), "scan <byte> <bit>" one bit only, "scan stop", "gmscan <from> <to>" / "kgmscan <from> <to>" (hex GM job ids on I-bus / K-bus), "kpscan <job> <from> <to>" (parameters of one job on the K-bus), "gmscan stop",
// "r <dest> <payload...>" raw hex frame from 0x3F on the I-bus, "kr ..." the same on the K-bus, e.g. "r BF 0C 00 00 00 C0 00 00 00 06"
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

  if (strcmp(line, "unlock on") == 0 || strcmp(line, "unlock off") == 0) {
    unlockOnIgnitionOff = strcmp(line, "unlock on") == 0;
    Serial.println(unlockOnIgnitionOff ? "UNLOCK ON IGNITION OFF: enabled" : "UNLOCK ON IGNITION OFF: disabled");
    return true;
  }

  if (strncmp(line, "hdr ", 4) == 0) {
    return handleHeaderGaugeCommand(line + 4);
  }

  if (strncmp(line, "gt ", 3) == 0) {
    return handleGtTextCommand(line + 3);
  }

  if (strcmp(line, "obc scan") == 0) {
    obcScanNextField = 1;
    obcScanNextMs = millis();
    Serial.println("OBC: requesting fields 01-0F, then type 'car' to list the answers");
    return true;
  }

  unsigned int obcField = 0;
  if (sscanf(line, "obc %x", &obcField) == 1 && obcField <= 0xFF) {
    requestObcField(obcField);
    return true;
  }

  if (strcmp(line, "car") == 0) {
    printCarGauges();
    return true;
  }

  if (strcmp(line, "car on") == 0 || strcmp(line, "car off") == 0) {
    carStreamEnabled = strcmp(line, "car on") == 0;
    Serial.println(carStreamEnabled ? "CAR: streaming every second" : "CAR: streaming off");
    return true;
  }

  unsigned int newUnlockJob = 0;
  if (sscanf(line, "unlockjob %x", &newUnlockJob) == 1 && newUnlockJob <= 0xFF) {
    unlockJob = newUnlockJob;
    Serial.printf("UNLOCK JOB on ignition off: %02X\n", unlockJob);
    return true;
  }

  if (strcmp(line, "lowbeam on") == 0 || strcmp(line, "lowbeam off") == 0) {
    lowBeamAfterIgnitionOff = strcmp(line, "lowbeam on") == 0;
    Serial.println(lowBeamAfterIgnitionOff ? "LOW BEAM AFTER IGNITION OFF: enabled" : "LOW BEAM AFTER IGNITION OFF: disabled");
    return true;
  }

  if (strcmp(line, "lowbeam test") == 0 || strcmp(line, "lowbeam stop") == 0) {
    if (line[8] == 't') {
      lowBeamHold.start(millis());
      Serial.println(">> LIGHTS: low beams hold started (test)");
    } else {
      stopLowBeamHold();
    }
    return true;
  }

  if (strncmp(line, "seat ", 5) == 0) {
    return handleSeatCommand(line + 5);
  }

  if (strncmp(line, "kpscan ", 7) == 0) {
    unsigned int job = 0, fromParam = 0, toParam = 0;
    if (!ibusTxEnabled || sscanf(line + 7, "%x %x %x", &job, &fromParam, &toParam) != 3 ||
        job > 0xFF || fromParam > toParam || toParam > 0xFF || isGmJobBlocked(job)) {
      Serial.println("KPSCAN: needs 'tx on' and 'kpscan <job> <from> <to>' in hex, job must not be a blocked id");
      return true;
    }
    gmScanBus = &kbus;
    gmScanFixedJob = job;
    memset(gmScanReplies, 0, sizeof(gmScanReplies));
    memset(gmScanSent, 0, sizeof(gmScanSent));
    gmScanSummaryPending = true;
    gmJobScanner.start(fromParam, toParam, millis(), false, 2000);
    Serial.printf("KPSCAN: job %02X, parameters %02X-%02X, one every 2000 ms, 'gmscan stop' to abort\n", job, fromParam, toParam);
    return true;
  }

  if (strcmp(line, "gmscan results") == 0) {
    printGmScanSummary();
    return true;
  }

  if (strcmp(line, "gmscan stop") == 0) {
    gmJobScanner.stop();
    Serial.println("GMSCAN: stopped");
    return true;
  }

  bool scanOnKbus = strncmp(line, "kgmscan ", 8) == 0;
  if (scanOnKbus || strncmp(line, "gmscan ", 7) == 0) {
    unsigned int fromJob = 0, toJob = 0;
    if (!ibusTxEnabled || sscanf(line + (scanOnKbus ? 8 : 7), "%x %x", &fromJob, &toJob) != 2 || fromJob > toJob || toJob > 0xFF) {
      Serial.println("GMSCAN: needs 'tx on' and '[k]gmscan <from> <to>' in hex, from <= to");
      return true;
    }
    gmScanBus = scanOnKbus ? &kbus : &ibus;
    gmScanFixedJob = -1;
    memset(gmScanReplies, 0, sizeof(gmScanReplies));
    memset(gmScanSent, 0, sizeof(gmScanSent));
    gmScanSummaryPending = true;
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

  int singleByteIndex = -1, singleBitIndex = -1;
  if (sscanf(line, "scan %d %d", &singleByteIndex, &singleBitIndex) == 2) {
    if (!ibusTxEnabled || !lcmIsScannableDataByte(singleByteIndex) || singleBitIndex < 0 || singleBitIndex > 7) {
      Serial.println("SCAN: needs 'tx on' and 'scan <byte 0-11 except 9,10> <bit 0-7>'");
      return true;
    }
    uint8_t payload[IBUS_MAX_PAYLOAD];
    uint8_t mask = 1 << singleBitIndex;
    uint8_t payloadLength = lcmBuildScanPayload(singleByteIndex, mask, lcmDimmerVoltage, lcmLoadRearVoltage, payload);
    bool queued = ibus.queueFrame(DIAGNOSTIC_ADDRESS, 0xD0, payload, payloadLength, false);
    Serial.printf(queued ? "SCAN ONE: byte %d bit %d (mask %02X) sent\n" : "SCAN ONE: busy, try again (byte %d bit %d mask %02X)\n",
                  singleByteIndex, singleBitIndex, mask);
    return true;
  }

  if (strncmp(line, "scan ", 5) == 0) {
    int byteIndex = atoi(line + 5);
    if (!ibusTxEnabled || byteIndex < 0 || !lcmIsScannableDataByte(byteIndex)) {
      Serial.println("SCAN: needs 'tx on' and a byte index 0-11 except 9 and 10");
      return true;
    }
    lcmBitScanner.start(byteIndex, millis());
    Serial.printf("SCAN: byte %d, one bit every %lu ms, 'scan stop' to abort\n",
                  byteIndex, (unsigned long)LcmBitScanner::STEP_MS);
    return true;
  }

  if (line[0] == 'r' && line[1] == ' ') {
    return handleRawTxCommand(line + 2, ibus);
  }

  if (line[0] == 'k' && line[1] == 'r' && line[2] == ' ') {
    return handleRawTxCommand(line + 3, kbus);
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

bool handleRawTxCommand(const char *hexBytes, IbusBus &bus) {
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

  bool queued = bus.queueFrame(DIAGNOSTIC_ADDRESS, bytes[0], bytes + 1, byteCount - 1);
  Serial.println(queued ? "TX: raw frame queued" : "TX: busy or too long");
  return true;
}

// "seat <move> [pulses]" (see SeatControl.h, ~1 s per pulse), "seat job <id> [param]" raw test, "seat stop"
bool handleSeatCommand(const char *arguments) {
  if (strcmp(arguments, "stop") == 0) {
    seatRepeater.stop();
    Serial.println("SEAT: stopped");
    return true;
  }

  if (!ibusTxEnabled) {
    Serial.println("SEAT: send 'tx on' first");
    return true;
  }

  unsigned int jobFromHex = 0, paramFromHex = 1;
  if (sscanf(arguments, "job %x %x", &jobFromHex, &paramFromHex) >= 1) {
    if (jobFromHex > 0xFF || paramFromHex > 0xFF || !isSeatJob(jobFromHex)) {
      Serial.println("SEAT: 'seat job <id> [param]' accepts only seat ids (low nibble 5 or 6, not trunk/window ids)");
      return true;
    }
    const uint8_t payload[] = {0x0C, (uint8_t)jobFromHex, (uint8_t)paramFromHex};
    bool queued = kbus.queueFrame(DIAGNOSTIC_ADDRESS, 0x00, payload, sizeof(payload));
    Serial.printf(queued ? "SEAT: job %02X param %02X sent\n" : "SEAT: busy, try again\n", (unsigned)jobFromHex, (unsigned)paramFromHex);
    return true;
  }

  char moveName[24] = "";
  unsigned int pulseCount = 1;
  sscanf(arguments, "%23s %u", moveName, &pulseCount);
  uint8_t param = seatParamForName(moveName);
  if (param == SEAT_NO_MOVE || pulseCount == 0) {
    Serial.println("SEAT: moves: forward back up down tilt_up tilt_down backrest_forward backrest_back headrest_up headrest_down; 'seat <move> [pulses]'");
    return true;
  }

  seatRepeater.start(param, pulseCount, millis());
  Serial.printf("SEAT: %s x%u (about %u ms each), 'seat stop' to abort\n", moveName, pulseCount, (unsigned)SEAT_PULSE_MS);
  return true;
}

// "hdr on|off", "hdr <zone 1|5|6> <speed|rpm|coolant|ambient|off>", "hdr show"
bool handleHeaderGaugeCommand(const char *arguments) {
  if (strcmp(arguments, "on") == 0 || strcmp(arguments, "off") == 0) {
    headerGaugesEnabled = strcmp(arguments, "on") == 0;
    memset(headerZoneLastSentMs, 0, sizeof(headerZoneLastSentMs));
    memset(headerZoneLastText, 0, sizeof(headerZoneLastText));
    Serial.println(headerGaugesEnabled ? "HDR: header gauges on (only while the radio shows AUX)" : "HDR: header gauges off");
    return true;
  }

  unsigned int zone = 0;
  char gaugeName[12] = "";
  if (sscanf(arguments, "%u %11s", &zone, gaugeName) == 2) {
    HeaderGauge gauge;
    for (HeaderZone &headerZone : headerZones) {
      if (headerZone.zone == zone && headerGaugeFromName(gaugeName, gauge)) {
        headerZone.gauge = gauge;
        memset(headerZoneLastText, 0, sizeof(headerZoneLastText));
        Serial.printf("HDR: zone %u -> %s\n", zone, gaugeName);
        return true;
      }
    }
  }
  Serial.println("HDR: 'hdr on|off' or 'hdr <1|5|6> <speed|rpm|coolant|ambient|off>'");
  return true;
}

// One zone per pass; re-sent every few seconds in case the radio redraws the header.
void updateHeaderGauges() {
  static uint8_t nextZoneIndex = 0;
  if (!headerGaugesEnabled || !radioShowsAux) return;

  uint32_t nowMs = millis();
  uint8_t index = nextZoneIndex;
  nextZoneIndex = (nextZoneIndex + 1) % HEADER_ZONE_COUNT;

  const HeaderZone &headerZone = headerZones[index];
  char text[HEADER_TEXT_MAX_LENGTH];
  if (!formatHeaderGauge(headerZone.gauge, carGauges, headerZone.width, text)) return;

  bool textChanged = strcmp(text, headerZoneLastText[index]) != 0;
  bool refreshDue = nowMs - headerZoneLastSentMs[index] >= HEADER_REFRESH_MS;
  if (!textChanged && !refreshDue) return;
  if (textChanged && nowMs - headerZoneLastSentMs[index] < HEADER_MIN_INTERVAL_MS) return;

  uint8_t payload[IBUS_MAX_PAYLOAD];
  payload[0] = 0xA5; payload[1] = 0x62; payload[2] = 0x01; payload[3] = headerZone.zone;
  uint8_t textLength = strlen(text);
  memcpy(payload + 4, text, textLength);
  if (!ibus.queueFrame(RADIO_ADDRESS, SCREEN_ADDRESS, payload, 4 + textLength, false)) return;

  strcpy(headerZoneLastText[index], text);
  headerZoneLastSentMs[index] = nowMs;
}

// Navigation screen text tests, sent as the radio (68 -> 3B), like BlueBus does:
//   gt zone <hex> <text>   A5 62 01 <zone> text    (cursor-style zone write, radio uses 41-46 and 07)
//   gt idx <hex> <text>    21 60 00 <index> text   (menu line, no cursor)
//   gt upd <hex>           A5 <type> 01 00         (refresh; BlueBus types: 60 index, 62 zone)
bool handleGtTextCommand(const char *arguments) {
  if (!ibusTxEnabled) {
    Serial.println("GT: send 'tx on' first");
    return true;
  }

  unsigned int number = 0;
  char text[24] = "";
  uint8_t payload[IBUS_MAX_PAYLOAD];
  uint8_t payloadLength = 0;

  if (sscanf(arguments, "zone %x %23[^\n]", &number, text) >= 1 && strncmp(arguments, "zone", 4) == 0) {
    payload[0] = 0xA5; payload[1] = 0x62; payload[2] = 0x01; payload[3] = number;
    payloadLength = 4;
  } else if (sscanf(arguments, "idx %x %23[^\n]", &number, text) >= 1 && strncmp(arguments, "idx", 3) == 0) {
    payload[0] = 0x21; payload[1] = 0x60; payload[2] = 0x00; payload[3] = number;
    payloadLength = 4;
  } else if (sscanf(arguments, "upd %x", &number) == 1 && strncmp(arguments, "upd", 3) == 0) {
    payload[0] = 0xA5; payload[1] = number; payload[2] = 0x01; payload[3] = 0x00;
    Serial.println(ibus.queueFrame(RADIO_ADDRESS, SCREEN_ADDRESS, payload, 4) ? "GT: update sent" : "GT: busy");
    return true;
  } else {
    Serial.println("GT: 'gt zone <hex> <text>', 'gt idx <hex> <text>', 'gt upd <hex>'");
    return true;
  }

  uint8_t textLength = strlen(text);
  memcpy(payload + payloadLength, text, textLength);
  Serial.println(ibus.queueFrame(RADIO_ADDRESS, SCREEN_ADDRESS, payload, payloadLength + textLength) ? "GT: text sent" : "GT: busy");
  return true;
}

// On-board computer request, like the navigation unit sends it: 3B -> 80, 41 <field> 01
bool queueObcRequest(uint8_t field) {
  const uint8_t payload[] = {0x41, field, 0x01};
  return ibus.queueFrame(0x3B, 0x80, payload, sizeof(payload), false);
}

void requestObcField(uint8_t field) {
  Serial.println(queueObcRequest(field) ? "OBC: request sent" : "OBC: busy, try again");
}

void updateObcScan() {
  if (obcScanNextField == 0 || (int32_t)(millis() - obcScanNextMs) < 0) return;
  if (!queueObcRequest(obcScanNextField)) return;

  obcScanNextMs = millis() + 400;
  obcScanNextField = obcScanNextField >= 0x0F ? 0 : obcScanNextField + 1;
}

void updateSeatMove() {
  uint32_t nowMs = millis();
  if (!seatRepeater.isPulseDue(nowMs)) return;

  const uint8_t payload[] = {0x0C, SEAT_JOB, seatRepeater.moveParam()};
  if (kbus.queueFrame(DIAGNOSTIC_ADDRESS, 0x00, payload, sizeof(payload), false)) {
    seatRepeater.markPulseSent(nowMs);
  }
}

void printCarGauges() {
  const CarGauges &g = carGauges;
  Serial.println("---- CAR ----");
  Serial.printf("ignition: %02X   reverse: %s   doors locked: %s (by car: %s)\n", ignitionStatus,
                reverseGearActive ? "ON" : "off", doorsLocked ? "yes" : "no", doorsLockedByCar ? "yes" : "no");
  if (g.hasSpeedRpm) Serial.printf("speed: %u km/h   rpm: %u   engine: %s\n", g.speedKmh, g.rpm, g.rpm > 0 ? "RUNNING" : "off");
  else Serial.println("speed/rpm: n/a (not received yet)");
  if (g.hasTemperatures) Serial.printf("coolant: %u C   ambient: %d C\n", g.coolantC, g.ambientC);
  else Serial.println("temperatures: n/a (not received yet)");
  if (g.hasOdometer) Serial.printf("odometer: %lu km\n", (unsigned long)g.odometerKm);
  if (g.hasLcmStatus) Serial.printf("battery: %u.%u V", g.batteryTenthsVolt / 10, g.batteryTenthsVolt % 10);
  else Serial.print("battery: n/a (send 'r D0 0B')");
  if (g.hasOilTemp) Serial.printf("   oil: %d C (unverified)\n", g.oilTempC);
  else Serial.println("   oil: n/a");
  for (uint8_t field = 0; field < OBC_TEXT_FIELD_COUNT; field++) {
    if (g.obcText[field][0] != '\0') Serial.printf("OBC %02X: %s\n", field, g.obcText[field]);
  }
  for (uint8_t slot = 0; slot < RAW_FRAME_SLOT_COUNT; slot++) {
    if (g.rawFrameLengths[slot] == 0) continue;
    Serial.printf("raw BF %02X:", RAW_FRAME_COMMANDS[slot]);
    for (uint8_t i = 4; i < g.rawFrameLengths[slot] - 1; i++) Serial.printf(" %02X", g.rawFrames[slot][i]);
    Serial.println();
  }
}

void updateCarStream() {
  if (!carStreamEnabled || millis() - lastCarStreamMs < 1000) return;
  lastCarStreamMs = millis();
  printCarGauges();
}

void stopLowBeamHold() {
  lowBeamHold.stop();
  lowBeamTerminatePending = true;
  Serial.println(">> LIGHTS: low beams hold stopped");
}

void updateLowBeamHold() {
  uint32_t nowMs = millis();

  if (lowBeamHold.isExpired(nowMs)) {
    stopLowBeamHold();
  }

  if (lowBeamTerminatePending) {
    if (ibus.queueFrame(DIAGNOSTIC_ADDRESS, 0xD0, DIAGNOSTIC_TERMINATE, sizeof(DIAGNOSTIC_TERMINATE), false)) {
      lowBeamTerminatePending = false;
    }
    return;
  }

  if (lowBeamHold.needsStatusRequest()) {
    if (ibus.queueFrame(DIAGNOSTIC_ADDRESS, 0xD0, LCM_IO_STATUS_REQUEST, sizeof(LCM_IO_STATUS_REQUEST), false)) {
      lowBeamHold.markStatusRequested();
    }
    return;
  }

  if (!lowBeamHold.isRefreshDue(nowMs)) return;

  uint8_t payload[IBUS_MAX_PAYLOAD];
  uint8_t payloadLength = lcmBuildScanPayload(LOW_BEAM_DATA_BYTE_INDEX, LOW_BEAM_BOTH_MASK,
                                              lcmDimmerVoltage, lcmLoadRearVoltage, payload);
  if (ibus.queueFrame(DIAGNOSTIC_ADDRESS, 0xD0, payload, payloadLength, false)) {
    lowBeamHold.markRefreshed(nowMs);
  }
}

void updateLcmBitScan() {
  if (!lcmBitScanner.isStepDue(millis())) return;

  uint8_t payload[IBUS_MAX_PAYLOAD];
  uint8_t payloadLength = lcmBuildScanPayload(lcmBitScanner.dataByteIndex(), lcmBitScanner.bitMask(),
                                              lcmDimmerVoltage, lcmLoadRearVoltage, payload);

  if (!ibus.queueFrame(DIAGNOSTIC_ADDRESS, 0xD0, payload, payloadLength)) return;

  Serial.printf("SCAN: byte %u mask %02X\n", lcmBitScanner.dataByteIndex(), lcmBitScanner.bitMask());
  lcmBitScanner.advance(millis());
}

void updateGmJobScan() {
  uint32_t nowMs = millis();

  if (gmScanSummaryPending && !gmJobScanner.isRunning() && nowMs - lastGmScanStepMs > 2500) {
    gmScanSummaryPending = false;
    printGmScanSummary();
  }

  if (!gmJobScanner.isStepDue(nowMs)) return;

  uint8_t job = gmJobScanner.job(); // the scanned value: job id, or parameter when a job is fixed
  const uint8_t payload[] = {0x0C, gmScanFixedJob >= 0 ? (uint8_t)gmScanFixedJob : job,
                             gmScanFixedJob >= 0 ? job : (uint8_t)0x01};
  if (!gmScanBus->queueFrame(DIAGNOSTIC_ADDRESS, 0x00, payload, sizeof(payload))) return;

  Serial.printf("GMSCAN: job %02X\n", job);
  lastScannedJob = job;
  gmScanSent[job] = true;
  gmScanReplies[job] = 0;
  lastGmScanStepMs = nowMs;
  gmJobScanner.advance(nowMs);
}

void recordGmReply(uint8_t replyCode) {
  bool belongsToScan = gmScanSent[lastScannedJob] && millis() - lastGmScanStepMs < 2000;
  if (!belongsToScan) {
    Serial.printf(">> GM REPLY %02X\n", replyCode);
    return;
  }
  gmScanReplies[lastScannedJob] = replyCode;
  Serial.printf(">> GM REPLY %02X (job %02X)\n", replyCode, lastScannedJob);
}

void printGmScanSummary() {
  Serial.println("GMSCAN SUMMARY (jobs sent -> GM reply)");
  uint16_t noReplyCount = 0;
  uint16_t replyCount[256] = {0};

  for (uint16_t job = 0; job < 256; job++) {
    if (!gmScanSent[job]) continue;
    if (gmScanReplies[job] == 0) noReplyCount++;
    else replyCount[gmScanReplies[job]]++;
  }

  for (uint16_t code = 1; code < 256; code++) {
    if (replyCount[code] == 0) continue;
    Serial.printf("  reply %02X (%u jobs):", (unsigned)code, (unsigned)replyCount[code]);
    for (uint16_t job = 0; job < 256; job++) {
      if (gmScanSent[job] && gmScanReplies[job] == code) Serial.printf(" %02X", (unsigned)job);
    }
    Serial.println();
  }

  Serial.printf("  no reply (%u jobs):", (unsigned)noReplyCount);
  for (uint16_t job = 0; job < 256; job++) {
    if (gmScanSent[job] && gmScanReplies[job] == 0) Serial.printf(" %02X", (unsigned)job);
  }
  Serial.println();
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
