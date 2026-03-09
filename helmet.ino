/*
  FINAL TEST CODE (Serial Monitor Only)
  ESP32-S3 Super Mini (pins available 0–13)

  MPU9250 (I2C) + MQ3 + MQ135 + Microphone + Buzzer
  - Prints all readings to Serial
  - BUZZER ON when (OR):
      MQ3 > ALCOHOL_THRESHOLD  OR  MQ135 > AIR_BAD_THRESHOLD  OR  MIC > MIC_THRESHOLD
  - Crash detection with anti-false-positive:
      * Impact = |AccelMag - 9.80665|
      * Needs multiple hits in a window
      * Optional stillness check using gyro
*/

#include <Wire.h>
#include <MPU9250.h>
#include <math.h>

// ---------------- PINS (0–13 only) ----------------
#define SDA_PIN 8
#define SCL_PIN 9

#define MQ3_PIN     1
#define MQ135_PIN   2
#define MIC_PIN     3

#define BUZZER_PIN  4
// --------------------------------------------------

// ---------------- THRESHOLDS (TUNE) ----------------
const int ALCOHOL_THRESHOLD = 2500; // MQ3 raw ADC threshold
const int AIR_BAD_THRESHOLD = 2500; // MQ135 raw ADC threshold
const int MIC_THRESHOLD     = 600;  // mic peak-to-peak threshold (TUNE)

// Crash detection (in m/s^2)
const float G = 9.80665f;               // gravity
const float IMPACT_THRESHOLD = 14.0f;   // m/s^2 (start 12–18)

// Require multiple hits in a window
const int HIT_REQUIRED = 3;
const int WINDOW_SIZE  = 5;

// Stillness confirmation (anti-false filter)
const bool USE_STILLNESS_CHECK = true;
const unsigned long STILLNESS_MS = 2000;

const float GYRO_STILL_THRESHOLD   = 80.0f; // deg/s (start 50–100)
const float IMPACT_STILL_THRESHOLD = 4.0f;  // m/s^2 (start 3–5)
// --------------------------------------------------

// Loop timing
uint32_t lastLoop = 0;
const int interval = 100; // ms

MPU9250 mpu;

// ---------------- MIC LEVEL ----------------
int getMicLevel() {
  int signalMax = 0;
  int signalMin = 4095;

  unsigned long start = millis();
  while (millis() - start < 50) {
    int sample = analogRead(MIC_PIN);
    if (sample > signalMax) signalMax = sample;
    if (sample < signalMin) signalMin = sample;
  }
  return signalMax - signalMin; // peak-to-peak
}

// -------- CRASH DETECTOR STATE --------
int hitCount = 0;
int windowCount = 0;
bool crashArmed = true;
unsigned long impactTime = 0;

// Returns true ONLY when crash is CONFIRMED
bool updateCrashDetector(float ax_ms2, float ay_ms2, float az_ms2,
                         float gx_dps, float gy_dps, float gz_dps) {
  float amag = sqrtf(ax_ms2 * ax_ms2 + ay_ms2 * ay_ms2 + az_ms2 * az_ms2);
  float impact = fabsf(amag - G);

  float gyroMag = sqrtf(gx_dps * gx_dps + gy_dps * gy_dps + gz_dps * gz_dps);

  // Windowed hit counting (debounce)
  windowCount++;
  if (impact > IMPACT_THRESHOLD) hitCount++;

  if (windowCount >= WINDOW_SIZE) {
    bool impactConfirmed = (hitCount >= HIT_REQUIRED);

    windowCount = 0;
    hitCount = 0;

    if (crashArmed && impactConfirmed) {
      impactTime = millis();
      crashArmed = false;

      if (!USE_STILLNESS_CHECK) {
        crashArmed = true;
        return true;
      }
    }
  }

  // Stillness confirmation stage
  if (USE_STILLNESS_CHECK && !crashArmed) {
    if (millis() - impactTime < STILLNESS_MS) {
      float amag_now = sqrtf(ax_ms2 * ax_ms2 + ay_ms2 * ay_ms2 + az_ms2 * az_ms2);
      float impact_now = fabsf(amag_now - G);

      if (gyroMag > GYRO_STILL_THRESHOLD || impact_now > IMPACT_STILL_THRESHOLD) {
        crashArmed = true; // cancel detection
      }
      return false;
    } else {
      crashArmed = true;
      return true;
    }
  }

  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // ADC setup
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(200);

  // MPU9250
  // Address usually 0x68. If your module uses 0x69, change here.
  if (!mpu.setup(0x68)) {
    Serial.println("❌ MPU9250 NOT detected! Try 0x69 if needed.");
    while (1) delay(10);
  }

  Serial.println("✅ MPU9250 Ready");

  // Optional calibration (run once if you want; keep sensor still)
  // mpu.calibrateAccelGyro();
  // mpu.calibrateMag();  // move in figure-8 during calibration

  Serial.println("\n===== FINAL SENSOR TEST START (MPU9250) =====");
}

void loop() {
  if (millis() - lastLoop < interval) return;
  lastLoop = millis();

  // Update MPU9250 readings
  if (!mpu.update()) return;

  // Accel in g from library -> convert to m/s^2
  float ax_ms2 = mpu.getAccX() * G;
  float ay_ms2 = mpu.getAccY() * G;
  float az_ms2 = mpu.getAccZ() * G;

  // Gyro in deg/s from library
  float gx_dps = mpu.getGyroX();
  float gy_dps = mpu.getGyroY();
  float gz_dps = mpu.getGyroZ();

  // Read gas + mic
  int mq3   = analogRead(MQ3_PIN);
  int mq135 = analogRead(MQ135_PIN);
  int mic   = getMicLevel();

  // Conditions
  bool alcohol = (mq3 > ALCOHOL_THRESHOLD);
  bool airPoor = (mq135 > AIR_BAD_THRESHOLD);
  bool loud    = (mic > MIC_THRESHOLD);

  // ✅ OR logic: buzzer ON if any is true
  bool buzzerOn = (alcohol || airPoor || loud);
  digitalWrite(BUZZER_PIN, buzzerOn ? HIGH : LOW);

  // Crash detection
  bool crash = updateCrashDetector(ax_ms2, ay_ms2, az_ms2, gx_dps, gy_dps, gz_dps);

  // Debug values
  float amag = sqrtf(ax_ms2 * ax_ms2 + ay_ms2 * ay_ms2 + az_ms2 * az_ms2);
  float impact = fabsf(amag - G);
  float gyroMag = sqrtf(gx_dps * gx_dps + gy_dps * gy_dps + gz_dps * gz_dps);

  // -------- SERIAL PRINT --------
  Serial.println("------------");
  Serial.print("AccelMag(m/s^2): "); Serial.print(amag, 2);
  Serial.print(" | Impact(|Mag-G|): "); Serial.println(impact, 2);

  Serial.print("GyroMag(deg/s): "); Serial.println(gyroMag, 1);

  Serial.print("MQ3 Alcohol: "); Serial.print(mq3);
  Serial.print(" -> "); Serial.println(alcohol ? "DETECTED ⚠️" : "Normal");

  Serial.print("MQ135 Air: "); Serial.print(mq135);
  Serial.print(" -> "); Serial.println(airPoor ? "POOR ⚠️" : "Good");

  Serial.print("Mic Level(p2p): "); Serial.print(mic);
  Serial.print(" -> "); Serial.println(loud ? "HIGH 🔊" : "Normal");

  Serial.print("Buzzer: ");
  Serial.println(buzzerOn ? "ON 🔊" : "OFF");

  Serial.print("Crash: ");
  Serial.println(crash ? "YES 🚨" : "No");

  // Optional: show orientation if you want (degrees)
  // Serial.print("Yaw/Pitch/Roll: ");
  // Serial.print(mpu.getYaw(), 1); Serial.print(", ");
  // Serial.print(mpu.getPitch(), 1); Serial.print(", ");
  // Serial.println(mpu.getRoll(), 1);
}