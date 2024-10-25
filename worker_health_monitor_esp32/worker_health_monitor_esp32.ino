#include <WiFi.h>  // Use <ESP8266WiFi.h> if using ESP8266
#include <heartRate.h>
#include <MAX30105.h>
#include <spo2_algorithm.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <math.h>  // For sqrt()
#include <HTTPClient.h>  // Include HTTPClient library

// WiFi credentials
#define WIFI_SSID "SSID"  // Replace with your WiFi SSID
#define WIFI_PASSWORD "PASS"  // Replace with your WiFi password

// Firebase configuration
const char* firebaseHost = "firebaseio.com";  // Use your Firebase project URL
const char* firebaseApiKey = "Seceret Key";  // Use your Firebase API key

// Initialize Sensors
Adafruit_MPU6050 mpu;
MAX30105 particleSensor;

// Variables for data
uint32_t irBuffer[100];  
uint32_t redBuffer[100];
int32_t bufferLength = 100; 
int32_t spo2; 
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;
int steps = 0;
float stressLevel;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  // Initialize MPU6050
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);

  // Initialize MAX30105
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30105 not found");
    while (1);
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);

  Serial.println("Setup complete.");
}

void loop() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Read temperature
  float temperature = temp.temperature;

  // Collect Heart Rate and SpO2 data
  for (int i = 0; i < bufferLength; i++) {
    while (!particleSensor.available()) particleSensor.check();
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();
  }

  maxim_heart_rate_and_oxygen_saturation(
    irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate
  );

  if (validHeartRate) stressLevel = calculateStressLevel(heartRate);

  if (detectStep(a.acceleration.x, a.acceleration.y, a.acceleration.z)) steps++;

  // Send data to Firebase at a static path
  String deviceID = "Device_1";  // Change for each device

  // Prepare the HTTP request
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    // Create JSON payload
    String jsonPayload = "{\"Temperature\":" + String(temperature) + ",";
    jsonPayload += "\"HeartRate\":" + String(heartRate) + ",";
    jsonPayload += "\"SpO2\":" + String(spo2) + ",";
    jsonPayload += "\"Steps\":" + String(steps) + ",";
    jsonPayload += "\"StressLevel\":" + String(stressLevel) + "}";

    // Specify the URL and HTTP method
    String url = String("https://") + firebaseHost + "/device1.json?auth=" + firebaseApiKey;  // Use .json at the end
    http.begin(url);  // Start connection
    http.addHeader("Content-Type", "application/json");  // Specify content type

    // Send HTTP PUT request to update existing data at a static path
    int httpResponseCode = http.PUT(jsonPayload);  // Use PUT to overwrite
    if (httpResponseCode > 0) {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      Serial.println("Data sent successfully");
    } else {
      Serial.print("Error sending data: ");
      Serial.println(httpResponseCode);
    }

    // End the connection
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }

  delay(1000);  // Delay for readability
}

bool detectStep(float x, float y, float z) {
  float accelThreshold = 1.2;  
  float magnitude = sqrt(x * x + y * y + z * z);
  return magnitude > accelThreshold;
}

float calculateStressLevel(int heartRate) {
  if (heartRate > 100) return 75.0;
  else if (heartRate > 80) return 50.0;
  else return 25.0;
}
