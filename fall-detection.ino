#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MPU6050.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <WiFi.h>
#include <ThingSpeak.h>

// ============================================================
//  CONFIG — fill these in
// ============================================================
const char* SSID         = "YOUR_WIFI_SSID";
const char* PASSWORD     = "YOUR_WIFI_PASSWORD";
unsigned long TS_CHANNEL = 0000000;        // your channel number
const char* TS_API_KEY   = "YOUR_WRITE_API_KEY";

// ============================================================
//  FALL DETECTION THRESHOLDS
// ============================================================
#define FREE_FALL_THRESH  0.3f   // g — below this = free-fall phase
#define IMPACT_THRESH     2.5f   // g — above this after free-fall = impact
#define FALL_WINDOW_MS    500    // ms — max time between free-fall and impact

// ============================================================
//  OBJECTS
// ============================================================
Adafruit_SSD1306 display(128, 64, &Wire, -1);
MPU6050 mpu;
MAX30105 particleSensor;
WiFiClient client;

// ============================================================
//  GLOBALS
// ============================================================
float heartRate        = 0;
bool  fallDetected     = false;
bool  inFreeFall       = false;
unsigned long fallStartTime = 0;
unsigned long lastUpload    = 0;
unsigned long lastBeat      = 0;

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);  // SDA=GPIO21, SCL=GPIO22

  // --- OLED ---
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found");
    while (true);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Initialising...");
  display.display();

  // --- MPU6050 ---
  mpu.initialize();
  mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_4); // ±4g range
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 not found");
  }

  // --- MAX30102 ---
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30102 not found");
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);

  // --- WiFi ---
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(SSID, PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }

  ThingSpeak.begin(client);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(WiFi.status() == WL_CONNECTED ? "WiFi Connected!" : "WiFi Failed!");
  display.display();
  delay(1000);
}

// ============================================================
//  LOOP
// ============================================================
void loop() {

  // ---- Read MPU6050 ----
  int16_t ax, ay, az, gx, gy, gz;
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  // Convert raw to g (±4g range → sensitivity = 8192 LSB/g)
  float aX = ax / 8192.0f;
  float aY = ay / 8192.0f;
  float aZ = az / 8192.0f;
  float mag = sqrt(aX*aX + aY*aY + aZ*aZ);

  // ---- Fall Detection ----
  if (mag < FREE_FALL_THRESH && !inFreeFall) {
    inFreeFall    = true;
    fallStartTime = millis();
  }
  if (inFreeFall) {
    if (mag > IMPACT_THRESH && (millis() - fallStartTime < FALL_WINDOW_MS)) {
      fallDetected = true;
      inFreeFall   = false;
      Serial.println("FALL DETECTED");
    }
    if (millis() - fallStartTime > FALL_WINDOW_MS) {
      inFreeFall = false;  // window expired, reset
    }
  }

  // ---- Read Heart Rate ----
  long irValue = particleSensor.getIR();
  if (checkForBeat(irValue)) {
    long delta = millis() - lastBeat;
    lastBeat    = millis();
    float bpm   = 60.0f / (delta / 1000.0f);
    if (bpm > 30 && bpm < 220) {
      heartRate = bpm;
    }
  }

  // ---- Update OLED ----
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("== Health Monitor ==");

  display.print("Heart Rate: ");
  display.print((int)heartRate);
  display.println(" bpm");

  display.print("Accel: ");
  display.print(mag, 2);
  display.println(" g");

  if (fallDetected) {
    display.setTextSize(2);
    display.setCursor(10, 42);
    display.println("!! FALL !!");
  } else {
    display.setTextSize(1);
    display.setCursor(0, 42);
    display.println("Status: Normal");
  }

  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print(WiFi.status() == WL_CONNECTED ? "WiFi: OK" : "WiFi: --");
  display.display();

  // ---- Upload to ThingSpeak every 15 seconds ----
  if (millis() - lastUpload > 15000) {
    lastUpload = millis();

    if (WiFi.status() == WL_CONNECTED) {
      ThingSpeak.setField(1, (int)heartRate);
      ThingSpeak.setField(2, mag);
      ThingSpeak.setField(3, fallDetected ? 1 : 0);

      int response = ThingSpeak.writeFields(TS_CHANNEL, TS_API_KEY);
      if (response == 200) {
        Serial.println("ThingSpeak upload OK");
      } else {
        Serial.print("ThingSpeak error: ");
        Serial.println(response);
      }
    }

    fallDetected = false;  // reset after upload
  }

  delay(100);
}