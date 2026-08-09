/*
  =====================================================================
  ULTIMATE SURPRISE FOR MS BABY - FRESH START v1.0
  =====================================================================

  Board:
    Adafruit Feather nRF52840 Express

  Sensors:
    MAX30102  - heart-rate optical sensor
    MCP9808   - contact-temperature sensor
    MPU6050   - motion sensor for the lunge challenge

  Display:
    Phone only over BLE Nordic UART Service (NUS)

  Power:
    USB during this build. No battery code is required.

  IMPORTANT:
    This sketch intentionally has:
      - NO LCD code
      - NO external LED code
      - NO battery code
      - NO empty I2C address-probe transactions

  Required extra Arduino library:
    SparkFun MAX3010x Pulse and Proximity Sensor Library

  The Adafruit nRF52 board package provides:
    bluefruit.h
    Wire

  If Arduino reports two libraries for MAX30105.h, REMOVE the
  DevLab_MAX30102 library and keep the SparkFun MAX3010x library.
*/

#include <bluefruit.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

// ---------------------------------------------------------------------
// Board sanity check
// ---------------------------------------------------------------------
#ifndef PIN_WIRE_SDA
  #error "PIN_WIRE_SDA is missing. Select Adafruit Bluefruit nRF52840 Feather Express."
#endif

#ifndef PIN_WIRE_SCL
  #error "PIN_WIRE_SCL is missing. Select Adafruit Bluefruit nRF52840 Feather Express."
#endif

// ---------------------------------------------------------------------
// Hardware / BLE constants
// ---------------------------------------------------------------------
static const char BLE_DEVICE_NAME[] = "ULTIMATE-SURPRISE";

static const uint8_t MCP9808_ADDRESS = 0x18;
static const uint8_t MAX30102_ADDRESS = 0x57;
static const uint8_t MPU6050_ADDRESS_1 = 0x68;
static const uint8_t MPU6050_ADDRESS_2 = 0x69;

static const uint32_t I2C_CLOCK_HZ = 100000UL;

// MAX30102 tuning.
// If finger detection is too strict, lower FINGER_IR_THRESHOLD slightly.
static const uint32_t FINGER_IR_THRESHOLD = 10000UL;

// Biometric challenge
static const uint32_t SCAN_DURATION_MS = 30000UL;
static const uint32_t TEMP_SAMPLE_INTERVAL_MS = 250UL;
static const uint32_t PHONE_PROGRESS_INTERVAL_MS = 500UL;
static const uint8_t MIN_VALID_BEATS = 5;
static const float MIN_FINGER_SAMPLE_RATIO = 0.35f;

static const float REQUIRED_HR_INCREASE_PERCENT = 10.0f;
static const float REQUIRED_TEMP_INCREASE_PERCENT = 5.0f;
static const bool REQUIRE_TEMP_TARGET = true;

// Lunge challenge
static const uint8_t REQUIRED_LUNGE_EVENTS_EACH_WAY = 5;
static const float LUNGE_ACCEL_THRESHOLD_MS2 = 1.35f;
static const uint32_t LUNGE_EVENT_REFRACTORY_MS = 500UL;
static const float TURN_REQUIRED_RADIANS = 2.60f;
static const uint32_t LUNGE_TIMEOUT_MS = 120000UL;
static const uint32_t MPU_SAMPLE_INTERVAL_MS = 20UL;

// Scan kind constants.
// Plain uint8_t constants avoid Arduino auto-prototype enum issues.
static const uint8_t SCAN_NONE = 0;
static const uint8_t SCAN_BASELINE = 1;
static const uint8_t SCAN_POST_PUSHUPS = 2;
static const uint8_t SCAN_POST_MOUNTAIN = 3;

// ---------------------------------------------------------------------
// BLE + sensor objects
// ---------------------------------------------------------------------
BLEUart bleuart;
MAX30105 max30102;

// ---------------------------------------------------------------------
// Sensor status
// ---------------------------------------------------------------------
bool i2cBusReady = false;
bool max30102OK = false;
bool mcp9808OK = false;
bool mpu6050OK = false;
uint8_t mpu6050Address = MPU6050_ADDRESS_1;

// ---------------------------------------------------------------------
// Stored challenge results
// ---------------------------------------------------------------------
float baselineBPM = 0.0f;
float baselineTempC = 0.0f;
bool baselineValid = false;

float postPushBPM = 0.0f;
float postPushTempC = 0.0f;

// ---------------------------------------------------------------------
// BLE command buffer
// ---------------------------------------------------------------------
char commandBuffer[48];
uint8_t commandLength = 0;
uint32_t commandLastByteMs = 0;

// ---------------------------------------------------------------------
// Active scan state
// ---------------------------------------------------------------------
bool scanActive = false;
uint8_t activeScanKind = SCAN_NONE;
uint32_t scanStartedMs = 0;
uint32_t lastTempSampleMs = 0;
uint32_t lastPhoneProgressMs = 0;

float scanBpmSum = 0.0f;
uint16_t scanValidBeatCount = 0;
float scanTempSumC = 0.0f;
uint16_t scanTempCount = 0;

uint32_t scanTotalHeartSamples = 0;
uint32_t scanFingerSamples = 0;

uint32_t latestIR = 0;
bool fingerPresent = false;
float latestTempC = 0.0f;

uint32_t lastBeatMs = 0;
float recentBPM[4] = {0, 0, 0, 0};
uint8_t recentBPMCount = 0;
uint8_t recentBPMIndex = 0;
float liveBPM = 0.0f;

// ---------------------------------------------------------------------
// Active lunge state
// ---------------------------------------------------------------------
bool lungeActive = false;
uint32_t lungeStartedMs = 0;
uint32_t lastMpuSampleMs = 0;
uint32_t lastLungeEventMs = 0;

uint8_t lungePhase = 0;  // 0=out, 1=turn, 2=back
uint8_t outLungeCount = 0;
uint8_t backLungeCount = 0;
bool accelerationWasHigh = false;

float turnAngleAccumulatedRad = 0.0f;
uint32_t previousTurnSampleMs = 0;

// ---------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------
void connectCallback(uint16_t connHandle);
void disconnectCallback(uint16_t connHandle, uint8_t reason);

void startAdvertising();
void initializeBluetooth();

void processCommand(const char* command);
void startScan(uint8_t kind);
void startLunges();
void resetGameState();

// =====================================================================
// Utility
// =====================================================================

const char* scanName(uint8_t kind) {
  if (kind == SCAN_BASELINE) return "BASELINE";
  if (kind == SCAN_POST_PUSHUPS) return "PUSHUPS";
  if (kind == SCAN_POST_MOUNTAIN) return "MOUNTAIN";
  return "NONE";
}

void sendLine(const char* message) {
  Serial.print("TX: ");
  Serial.println(message);

  if (Bluefruit.connected()) {
    bleuart.println(message);
  }
}

void sendLine(const String& message) {
  Serial.print("TX: ");
  Serial.println(message);

  if (Bluefruit.connected()) {
    bleuart.println(message);
  }
}

void sendStatus() {
  String line = "STATUS,";
  line += max30102OK ? "1" : "0";
  line += ",";
  line += mcp9808OK ? "1" : "0";
  line += ",";
  line += mpu6050OK ? "1" : "0";
  sendLine(line);
}

void sendError(const char* code) {
  String line = "ERROR,";
  line += code;
  sendLine(line);
}

// =====================================================================
// I2C - safe startup and direct register helpers
// =====================================================================

bool i2cLinesReleased() {
  pinMode(PIN_WIRE_SDA, INPUT_PULLUP);
  pinMode(PIN_WIRE_SCL, INPUT_PULLUP);
  delay(10);

  return digitalRead(PIN_WIRE_SDA) == HIGH &&
         digitalRead(PIN_WIRE_SCL) == HIGH;
}

bool i2cWriteReg8(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission(true) == 0;
}

bool i2cReadBytes(uint8_t address, uint8_t startReg, uint8_t* destination, uint8_t length) {
  if (destination == nullptr || length == 0) return false;

  // Always write a real register address before endTransmission().
  // This intentionally avoids an empty I2C "probe" transaction.
  Wire.beginTransmission(address);
  Wire.write(startReg);

  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  uint8_t received = (uint8_t)Wire.requestFrom(address, length);

  if (received != length) {
    while (Wire.available()) {
      (void)Wire.read();
    }
    return false;
  }

  for (uint8_t i = 0; i < length; i++) {
    if (!Wire.available()) return false;
    destination[i] = (uint8_t)Wire.read();
  }

  return true;
}

bool i2cReadReg8(uint8_t address, uint8_t reg, uint8_t& value) {
  return i2cReadBytes(address, reg, &value, 1);
}

// =====================================================================
// MCP9808 direct driver
// =====================================================================

bool initializeMCP9808() {
  uint8_t manufacturerBytes[2] = {0, 0};

  // Manufacturer ID register 0x06 should contain 0x0054.
  if (!i2cReadBytes(MCP9808_ADDRESS, 0x06, manufacturerBytes, 2)) {
    return false;
  }

  uint16_t manufacturerID =
    ((uint16_t)manufacturerBytes[0] << 8) |
    manufacturerBytes[1];

  if (manufacturerID != 0x0054) {
    return false;
  }

  // Resolution register 0x08 = 3 -> 0.0625 C resolution.
  return i2cWriteReg8(MCP9808_ADDRESS, 0x08, 0x03);
}

bool readMCP9808TempC(float& tempC) {
  uint8_t data[2] = {0, 0};

  if (!i2cReadBytes(MCP9808_ADDRESS, 0x05, data, 2)) {
    return false;
  }

  uint16_t raw =
    ((uint16_t)(data[0] & 0x1F) << 8) |
    data[1];

  if (raw & 0x1000) {
    // Negative value: clear sign bit, then subtract 256 C.
    tempC = ((float)(raw & 0x0FFF) / 16.0f) - 256.0f;
  } else {
    tempC = (float)raw / 16.0f;
  }

  return true;
}

// =====================================================================
// MPU6050 direct driver
// =====================================================================

bool initializeMPU6050At(uint8_t address) {
  uint8_t whoAmI = 0;

  if (!i2cReadReg8(address, 0x75, whoAmI)) {
    return false;
  }

  // Common MPU6050 parts report 0x68. Some variants/address states may
  // expose 0x69, so accept both.
  if (whoAmI != 0x68 && whoAmI != 0x69) {
    return false;
  }

  // Wake up.
  if (!i2cWriteReg8(address, 0x6B, 0x00)) return false;
  delay(50);

  // DLPF around 20/21 Hz.
  if (!i2cWriteReg8(address, 0x1A, 0x04)) return false;

  // Gyro +/-500 deg/s.
  if (!i2cWriteReg8(address, 0x1B, 0x08)) return false;

  // Accelerometer +/-8 g.
  if (!i2cWriteReg8(address, 0x1C, 0x10)) return false;

  return true;
}

bool readMPU6050(
  float& ax,
  float& ay,
  float& az,
  float& gxRadS,
  float& gyRadS,
  float& gzRadS
) {
  uint8_t data[14];

  if (!i2cReadBytes(mpu6050Address, 0x3B, data, 14)) {
    return false;
  }

  int16_t rawAx = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
  int16_t rawAy = (int16_t)(((uint16_t)data[2] << 8) | data[3]);
  int16_t rawAz = (int16_t)(((uint16_t)data[4] << 8) | data[5]);

  int16_t rawGx = (int16_t)(((uint16_t)data[8] << 8) | data[9]);
  int16_t rawGy = (int16_t)(((uint16_t)data[10] << 8) | data[11]);
  int16_t rawGz = (int16_t)(((uint16_t)data[12] << 8) | data[13]);

  // +/-8 g -> 4096 LSB/g.
  const float accelScale = 9.80665f / 4096.0f;
  ax = rawAx * accelScale;
  ay = rawAy * accelScale;
  az = rawAz * accelScale;

  // +/-500 deg/s -> 65.5 LSB/(deg/s), then convert to rad/s.
  const float gyroScale = 0.017453292519943295f / 65.5f;
  gxRadS = rawGx * gyroScale;
  gyRadS = rawGy * gyroScale;
  gzRadS = rawGz * gyroScale;

  return true;
}

// =====================================================================
// Sensor initialization
// =====================================================================

void initializeSensors() {
  Serial.println("Starting sensor initialization...");

  max30102OK = false;
  mcp9808OK = false;
  mpu6050OK = false;

  if (!i2cLinesReleased()) {
    Serial.println("I2C ERROR: SDA or SCL is being held LOW.");
    Serial.println("Skipping sensor transactions so the firmware cannot freeze.");
    i2cBusReady = false;
    return;
  }

  Serial.println("I2C lines released: OK");

  Wire.begin();
  Wire.setClock(I2C_CLOCK_HZ);
  delay(100);
  i2cBusReady = true;

  Serial.print("MCP9808: ");
  mcp9808OK = initializeMCP9808();
  Serial.println(mcp9808OK ? "OK" : "ERROR");

  Serial.print("MAX30102: ");
  max30102OK = max30102.begin(Wire, I2C_SPEED_STANDARD);

  if (max30102OK) {
    // brightness, average, LED mode, sample rate, pulse width, ADC range
    max30102.setup(60, 4, 2, 100, 411, 4096);
    max30102.setPulseAmplitudeRed(0x1F);
    max30102.setPulseAmplitudeIR(0x1F);
    max30102.clearFIFO();
    Serial.println("OK");
  } else {
    Serial.println("ERROR");
  }

  Serial.print("MPU6050: ");

  if (initializeMPU6050At(MPU6050_ADDRESS_1)) {
    mpu6050Address = MPU6050_ADDRESS_1;
    mpu6050OK = true;
  } else if (initializeMPU6050At(MPU6050_ADDRESS_2)) {
    mpu6050Address = MPU6050_ADDRESS_2;
    mpu6050OK = true;
  }

  if (mpu6050OK) {
    Serial.print("OK at 0x");
    Serial.println(mpu6050Address, HEX);
  } else {
    Serial.println("ERROR");
  }

  Serial.println("Sensor initialization finished.");
}

// =====================================================================
// Bluetooth
// =====================================================================

void connectCallback(uint16_t connHandle) {
  (void)connHandle;
  Serial.println("BLE PHONE CONNECTED");
}

void disconnectCallback(uint16_t connHandle, uint8_t reason) {
  (void)connHandle;

  Serial.print("BLE PHONE DISCONNECTED, reason=0x");
  Serial.println(reason, HEX);
}

void startAdvertising() {
  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();

  Bluefruit.Advertising.addFlags(
    BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE
  );
  Bluefruit.Advertising.addTxPower();

  // Nordic UART Service UUID is included in the advertising packet.
  Bluefruit.Advertising.addService(bleuart);

  // Device name goes in scan response.
  Bluefruit.ScanResponse.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);

  // Advertise forever until connected.
  Bluefruit.Advertising.start(0);

  Serial.print("BLE advertising started: ");
  Serial.println(BLE_DEVICE_NAME);
}

void initializeBluetooth() {
  Serial.println("Starting Bluetooth...");

  // No physical game LEDs. Disable the automatic connection LED behavior.
  Bluefruit.autoConnLed(false);

  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName(BLE_DEVICE_NAME);

  Bluefruit.Periph.setConnectCallback(connectCallback);
  Bluefruit.Periph.setDisconnectCallback(disconnectCallback);

  bleuart.begin();
  startAdvertising();
}

// =====================================================================
// Command handling
// =====================================================================

bool isKnownCommand(const char* command) {
  return
    strcmp(command, "PING") == 0 ||
    strcmp(command, "STATUS") == 0 ||
    strcmp(command, "START_BASELINE") == 0 ||
    strcmp(command, "START_POST_PUSHUPS") == 0 ||
    strcmp(command, "START_POST_MOUNTAIN") == 0 ||
    strcmp(command, "START_LUNGES") == 0 ||
    strcmp(command, "ACCEPT") == 0 ||
    strcmp(command, "DECLINE") == 0 ||
    strcmp(command, "RESTART") == 0;
}

void clearCommandBuffer() {
  commandLength = 0;
  commandBuffer[0] = '\0';
}

void processBufferedCommand() {
  if (commandLength == 0) return;

  commandBuffer[commandLength] = '\0';
  processCommand(commandBuffer);
  clearCommandBuffer();
}

void readBLECommands() {
  while (bleuart.available()) {
    char c = (char)bleuart.read();
    commandLastByteMs = millis();

    if (c == '\n' || c == '\r') {
      processBufferedCommand();
      continue;
    }

    if (c < 32 || c > 126) {
      continue;
    }

    if (commandLength < sizeof(commandBuffer) - 1) {
      commandBuffer[commandLength++] = c;
      commandBuffer[commandLength] = '\0';

      // Process immediately once an exact known command is assembled.
      if (isKnownCommand(commandBuffer)) {
        processBufferedCommand();
      }
    } else {
      clearCommandBuffer();
      sendError("COMMAND_TOO_LONG");
    }
  }

  // Also accept a complete command if a browser omitted a newline.
  if (
    commandLength > 0 &&
    millis() - commandLastByteMs >= 100UL
  ) {
    processBufferedCommand();
  }
}

void processCommand(const char* command) {
  Serial.print("RX COMMAND: ");
  Serial.println(command);

  String ack = "COMMAND_ACK,";
  ack += command;
  sendLine(ack);

  if (strcmp(command, "PING") == 0) {
    sendLine("PONG");
  }
  else if (strcmp(command, "STATUS") == 0) {
    sendStatus();
  }
  else if (strcmp(command, "START_BASELINE") == 0) {
    startScan(SCAN_BASELINE);
  }
  else if (strcmp(command, "START_POST_PUSHUPS") == 0) {
    startScan(SCAN_POST_PUSHUPS);
  }
  else if (strcmp(command, "START_POST_MOUNTAIN") == 0) {
    startScan(SCAN_POST_MOUNTAIN);
  }
  else if (strcmp(command, "START_LUNGES") == 0) {
    startLunges();
  }
  else if (strcmp(command, "ACCEPT") == 0) {
    sendLine("ACCEPTED");
  }
  else if (strcmp(command, "DECLINE") == 0) {
    sendLine("DECLINED");
  }
  else if (strcmp(command, "RESTART") == 0) {
    resetGameState();
    sendLine("RESET_DONE");
  }
  else {
    sendError("UNKNOWN_COMMAND");
  }
}

// =====================================================================
// Heart-rate processing
// =====================================================================

void resetHeartTracking() {
  lastBeatMs = 0;
  recentBPMCount = 0;
  recentBPMIndex = 0;
  liveBPM = 0.0f;

  for (uint8_t i = 0; i < 4; i++) {
    recentBPM[i] = 0.0f;
  }
}

void addRecentBPM(float bpm) {
  recentBPM[recentBPMIndex] = bpm;
  recentBPMIndex = (recentBPMIndex + 1) % 4;

  if (recentBPMCount < 4) {
    recentBPMCount++;
  }

  float sum = 0.0f;
  for (uint8_t i = 0; i < recentBPMCount; i++) {
    sum += recentBPM[i];
  }

  liveBPM = sum / recentBPMCount;
}

void processHeartSample() {
  if (!max30102OK) return;

  uint32_t ir = max30102.getIR();
  latestIR = ir;

  fingerPresent = ir >= FINGER_IR_THRESHOLD;

  scanTotalHeartSamples++;

  if (fingerPresent) {
    scanFingerSamples++;
  }

  if (!fingerPresent) {
    return;
  }

  if (checkForBeat(ir)) {
    uint32_t now = millis();

    if (lastBeatMs != 0) {
      uint32_t deltaMs = now - lastBeatMs;

      if (deltaMs > 0) {
        float bpm = 60000.0f / (float)deltaMs;

        if (bpm >= 40.0f && bpm <= 200.0f) {
          addRecentBPM(bpm);

          scanBpmSum += bpm;
          scanValidBeatCount++;
        }
      }
    }

    lastBeatMs = now;
  }
}

// =====================================================================
// 30-second biometric scans
// =====================================================================

void startScan(uint8_t kind) {
  if (!max30102OK || !mcp9808OK) {
    sendError("BIOMETRIC_SENSOR");
    sendStatus();
    return;
  }

  // A repeated command from the website should confirm the existing scan,
  // not restart its 30-second timer.
  if (scanActive) {
    if (activeScanKind == kind) {
      String started = "SCAN_STARTED,";
      started += scanName(kind);
      sendLine(started);
    } else {
      sendError("SCAN_ALREADY_ACTIVE");
    }
    return;
  }

  lungeActive = false;

  activeScanKind = kind;
  scanActive = true;
  scanStartedMs = millis();
  lastTempSampleMs = 0;
  lastPhoneProgressMs = 0;

  scanBpmSum = 0.0f;
  scanValidBeatCount = 0;
  scanTempSumC = 0.0f;
  scanTempCount = 0;

  scanTotalHeartSamples = 0;
  scanFingerSamples = 0;

  latestIR = 0;
  fingerPresent = false;
  latestTempC = 0.0f;

  resetHeartTracking();
  max30102.clearFIFO();

  String started = "SCAN_STARTED,";
  started += scanName(kind);
  sendLine(started);
}

void finishScan() {
  uint8_t completedKind = activeScanKind;

  scanActive = false;
  activeScanKind = SCAN_NONE;

  float fingerRatio = 0.0f;

  if (scanTotalHeartSamples > 0) {
    fingerRatio =
      (float)scanFingerSamples /
      (float)scanTotalHeartSamples;
  }

  bool heartSignalGood =
    scanValidBeatCount >= MIN_VALID_BEATS &&
    fingerRatio >= MIN_FINGER_SAMPLE_RATIO;

  bool tempSignalGood = scanTempCount >= 20;

  if (!heartSignalGood || !tempSignalGood) {
    String retry = "SCAN_RETRY,";
    retry += scanName(completedKind);
    retry += ",";

    if (!heartSignalGood && !tempSignalGood) {
      retry += "HEART_AND_TEMP_SIGNAL";
    } else if (!heartSignalGood) {
      retry += "HEART_SIGNAL";
    } else {
      retry += "TEMP_SIGNAL";
    }

    sendLine(retry);
    return;
  }

  float avgBPM = scanBpmSum / (float)scanValidBeatCount;
  float avgTempC = scanTempSumC / (float)scanTempCount;

  String done = "SCAN_DONE,";
  done += scanName(completedKind);
  done += ",";
  done += String(avgBPM, 1);
  done += ",";
  done += String(avgTempC, 2);
  sendLine(done);

  if (completedKind == SCAN_BASELINE) {
    baselineBPM = avgBPM;
    baselineTempC = avgTempC;
    baselineValid = true;
  }
  else if (completedKind == SCAN_POST_PUSHUPS) {
    postPushBPM = avgBPM;
    postPushTempC = avgTempC;
  }
  else if (completedKind == SCAN_POST_MOUNTAIN) {
    if (!baselineValid || baselineBPM <= 0.0f || baselineTempC == 0.0f) {
      sendError("NO_BASELINE");
      return;
    }

    float hrPercent =
      ((avgBPM - baselineBPM) / baselineBPM) * 100.0f;

    float tempPercent =
      ((avgTempC - baselineTempC) / baselineTempC) * 100.0f;

    bool hrPass = hrPercent >= REQUIRED_HR_INCREASE_PERCENT;
    bool tempPass = tempPercent >= REQUIRED_TEMP_INCREASE_PERCENT;

    bool overallPass =
      hrPass &&
      (!REQUIRE_TEMP_TARGET || tempPass);

    String result = "INTENSITY,";
    result += overallPass ? "PASS" : "FAIL";
    result += ",";
    result += String(hrPercent, 1);
    result += ",";
    result += String(tempPercent, 1);
    result += ",";
    result += hrPass ? "1" : "0";
    result += ",";
    result += tempPass ? "1" : "0";

    sendLine(result);
  }
}

void updateScan() {
  if (!scanActive) return;

  uint32_t now = millis();
  uint32_t elapsed = now - scanStartedMs;

  // One optical sample per loop. SparkFun's heart-rate example also
  // obtains IR data directly from getIR().
  processHeartSample();

  if (
    lastTempSampleMs == 0 ||
    now - lastTempSampleMs >= TEMP_SAMPLE_INTERVAL_MS
  ) {
    lastTempSampleMs = now;

    float tempC = 0.0f;

    if (
      readMCP9808TempC(tempC) &&
      !isnan(tempC) &&
      tempC >= -40.0f &&
      tempC <= 125.0f
    ) {
      latestTempC = tempC;
      scanTempSumC += tempC;
      scanTempCount++;
    }
  }

  if (
    lastPhoneProgressMs == 0 ||
    now - lastPhoneProgressMs >= PHONE_PROGRESS_INTERVAL_MS
  ) {
    lastPhoneProgressMs = now;

    uint32_t percent =
      (elapsed >= SCAN_DURATION_MS)
      ? 100UL
      : (elapsed * 100UL) / SCAN_DURATION_MS;

    uint32_t remainingMs =
      (elapsed >= SCAN_DURATION_MS)
      ? 0UL
      : SCAN_DURATION_MS - elapsed;

    uint8_t secondsRemaining =
      (uint8_t)((remainingMs + 999UL) / 1000UL);

    String progress = "SCAN_PROGRESS,";
    progress += scanName(activeScanKind);
    progress += ",";
    progress += String((uint8_t)percent);
    progress += ",";
    progress += String(liveBPM, 0);
    progress += ",";
    progress += String(latestTempC, 2);
    progress += ",";
    progress += fingerPresent ? "1" : "0";
    progress += ",";
    progress += String(secondsRemaining);
    progress += ",";
    progress += String(latestIR);

    sendLine(progress);
  }

  if (elapsed >= SCAN_DURATION_MS) {
    finishScan();
  }
}

// =====================================================================
// Walking-lunge challenge
// =====================================================================

void sendLungeProgress() {
  uint8_t percent = 0;

  if (lungePhase == 0) {
    int calculated =
      (int)outLungeCount * 45 /
      REQUIRED_LUNGE_EVENTS_EACH_WAY;

    if (calculated > 45) calculated = 45;
    if (calculated < 0) calculated = 0;
    percent = (uint8_t)calculated;
  }
  else if (lungePhase == 1) {
    percent = 50;
  }
  else {
    int calculated =
      55 +
      (int)backLungeCount * 45 /
      REQUIRED_LUNGE_EVENTS_EACH_WAY;

    if (calculated > 100) calculated = 100;
    if (calculated < 55) calculated = 55;
    percent = (uint8_t)calculated;
  }

  String line = "LUNGE_PROGRESS,";
  line += String(percent);
  line += ",";
  line += String(outLungeCount);
  line += ",";
  line += String(backLungeCount);
  line += ",";
  line += lungePhase >= 2 ? "1" : "0";

  sendLine(line);
}

void startLunges() {
  if (!mpu6050OK) {
    sendError("MPU6050");
    sendStatus();
    return;
  }

  if (lungeActive) {
    sendLine("LUNGE_STARTED");
    sendLungeProgress();
    return;
  }

  scanActive = false;
  activeScanKind = SCAN_NONE;

  lungeActive = true;
  lungeStartedMs = millis();
  lastMpuSampleMs = 0;
  lastLungeEventMs = 0;

  lungePhase = 0;
  outLungeCount = 0;
  backLungeCount = 0;
  accelerationWasHigh = false;

  turnAngleAccumulatedRad = 0.0f;
  previousTurnSampleMs = 0;

  sendLine("LUNGE_STARTED");
  sendLungeProgress();
}

void updateLunges() {
  if (!lungeActive) return;

  uint32_t now = millis();

  if (now - lungeStartedMs >= LUNGE_TIMEOUT_MS) {
    lungeActive = false;
    sendLine("LUNGE_TIMEOUT");
    return;
  }

  if (
    lastMpuSampleMs != 0 &&
    now - lastMpuSampleMs < MPU_SAMPLE_INTERVAL_MS
  ) {
    return;
  }

  lastMpuSampleMs = now;

  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;
  float gx = 0.0f;
  float gy = 0.0f;
  float gz = 0.0f;

  if (!readMPU6050(ax, ay, az, gx, gy, gz)) {
    return;
  }

  float accelMagnitude =
    sqrt(ax * ax + ay * ay + az * az);

  float dynamicAcceleration =
    fabs(accelMagnitude - 9.80665f);

  bool accelerationHigh =
    dynamicAcceleration >= LUNGE_ACCEL_THRESHOLD_MS2;

  // Count a lunge-like movement on a new threshold crossing with
  // a refractory period to avoid counting the same motion repeatedly.
  bool newMovementEvent =
    accelerationHigh &&
    !accelerationWasHigh &&
    (
      lastLungeEventMs == 0 ||
      now - lastLungeEventMs >= LUNGE_EVENT_REFRACTORY_MS
    );

  accelerationWasHigh = accelerationHigh;

  if (lungePhase == 0 && newMovementEvent) {
    lastLungeEventMs = now;

    if (outLungeCount < REQUIRED_LUNGE_EVENTS_EACH_WAY) {
      outLungeCount++;
      sendLungeProgress();
    }

    if (outLungeCount >= REQUIRED_LUNGE_EVENTS_EACH_WAY) {
      lungePhase = 1;
      turnAngleAccumulatedRad = 0.0f;
      previousTurnSampleMs = now;
      sendLine("OUTBOUND_COMPLETE");
      sendLungeProgress();
    }

    return;
  }

  if (lungePhase == 1) {
    if (previousTurnSampleMs == 0) {
      previousTurnSampleMs = now;
    }

    float dt =
      (float)(now - previousTurnSampleMs) / 1000.0f;

    previousTurnSampleMs = now;

    float angularSpeedMagnitude =
      sqrt(gx * gx + gy * gy + gz * gz);

    turnAngleAccumulatedRad += angularSpeedMagnitude * dt;

    if (turnAngleAccumulatedRad >= TURN_REQUIRED_RADIANS) {
      lungePhase = 2;
      accelerationWasHigh = false;
      lastLungeEventMs = now;
      sendLine("TURN_DETECTED");
      sendLungeProgress();
    }

    return;
  }

  if (lungePhase == 2 && newMovementEvent) {
    lastLungeEventMs = now;

    if (backLungeCount < REQUIRED_LUNGE_EVENTS_EACH_WAY) {
      backLungeCount++;
      sendLungeProgress();
    }

    if (backLungeCount >= REQUIRED_LUNGE_EVENTS_EACH_WAY) {
      lungeActive = false;
      sendLine("LUNGE_DONE");
    }
  }
}

// =====================================================================
// Game reset
// =====================================================================

void resetGameState() {
  scanActive = false;
  activeScanKind = SCAN_NONE;
  lungeActive = false;

  baselineBPM = 0.0f;
  baselineTempC = 0.0f;
  baselineValid = false;

  postPushBPM = 0.0f;
  postPushTempC = 0.0f;

  resetHeartTracking();

  if (max30102OK) {
    max30102.clearFIFO();
  }
}

// =====================================================================
// Arduino setup / loop
// =====================================================================

void setup() {
  Serial.begin(115200);

  // Do not permanently block if Serial Monitor is closed.
  uint32_t serialWaitStarted = millis();
  while (!Serial && millis() - serialWaitStarted < 1500UL) {
    delay(10);
  }

  Serial.println();
  Serial.println("====================================================");
  Serial.println("ULTIMATE SURPRISE FOR MS BABY - FRESH START v1.0");
  Serial.println("====================================================");

  // Sensors are initialized first so the phone cannot connect halfway
  // through startup. Failed sensors do NOT prevent Bluetooth from starting.
  initializeSensors();

  initializeBluetooth();

  Serial.println();
  Serial.println("READY");
  Serial.print("BLE name: ");
  Serial.println(BLE_DEVICE_NAME);
  Serial.print("MAX30102: ");
  Serial.println(max30102OK ? "OK" : "ERROR");
  Serial.print("MCP9808:  ");
  Serial.println(mcp9808OK ? "OK" : "ERROR");
  Serial.print("MPU6050:  ");
  Serial.println(mpu6050OK ? "OK" : "ERROR");
  Serial.println("====================================================");
}

void loop() {
  readBLECommands();

  if (scanActive) {
    updateScan();
  }

  if (lungeActive) {
    updateLunges();
  }

  static uint32_t lastHeartbeatMs = 0;

  if (millis() - lastHeartbeatMs >= 5000UL) {
    lastHeartbeatMs = millis();

    Serial.print("HEARTBEAT | BLE=");
    Serial.print(Bluefruit.connected() ? "CONNECTED" : "ADVERTISING");
    Serial.print(" | MAX=");
    Serial.print(max30102OK ? "OK" : "ERR");
    Serial.print(" MCP=");
    Serial.print(mcp9808OK ? "OK" : "ERR");
    Serial.print(" MPU=");
    Serial.println(mpu6050OK ? "OK" : "ERR");
  }
}
