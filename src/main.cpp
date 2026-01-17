#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "INA226.h"
#include "CharlieplexDisplay.h"

// INA226 I2C address (default is 0x40, verify with your module)
#define INA226_ADDRESS 0x40

// I2C pins for ESP32
#define SDA_PIN 21
#define SCL_PIN 22

// Shunt resistor value
#define SHUNT_RESISTOR 0.0015  // 0.0015 Ohm (1.5 milliohm)

// Battery specifications (configurable via web UI)
float batteryCapacityAh = 300.0;  // Default 300Ah, user configurable
unsigned long logIntervalMs = 10 * 60 * 1000;  // Default 10 minutes, user configurable

#define PEUKERT_EXPONENT 1.1       // Peukert exponent for lead acid (typically 1.05-1.4)
// C20_RATE calculated as batteryCapacityAh / 20.0 at runtime
#define FULL_VOLTAGE_THRESHOLD 13.8  // Voltage threshold for "full" detection
#define FULL_CURRENT_THRESHOLD 1.0   // Current below this (in A) indicates full when voltage high
#define FULL_DETECTION_TIME 60000    // Must meet criteria for 60 seconds

// SOC calculation settings
#define SOC_CALC_INTERVAL_MS 10000  // Calculate SOC every 10 seconds

// WiFi AP settings
const char* ssid = "f-power";
const char* password = "";  // No password

// Data logging settings
#define MAX_DATA_POINTS 288  // 48 hours at 10-minute intervals (48*6)

// Display refresh rate
#define REFRESH_INTERVAL_MS 0  // 0 = fastest, increase if needed (1, 2, 5, 10 ms)

// Data structure
struct DataPoint {
  unsigned long timestamp;  // Minutes since boot
  float voltage;
  float current;
  float soc;  // State of charge percentage
};

DataPoint dataLog[MAX_DATA_POINTS];
int dataIndex = 0;
int dataCount = 0;
unsigned long lastLogTime = 0;
unsigned long bootTime = 0;

// SOC tracking variables
float socPercentage = 100.0;  // Current state of charge percentage
float ampHoursRemaining = 300.0;  // Amp-hours remaining (will be set to batteryCapacityAh)
unsigned long lastSocCalcTime = 0;
unsigned long fullDetectionStartTime = 0;
bool batteryWasFull = false;

// Charlieplex display
CharlieplexDisplay display;

INA226 ina(INA226_ADDRESS);
AsyncWebServer server(80);

// File paths for data storage
const char* dataFilePath = "/datalog.bin";
const char* socFilePath = "/soc.bin";
const char* settingsFilePath = "/settings.bin";

// Forward declarations
void saveData();
bool loadData();
void saveSoc();
bool loadSoc();
void saveSettings();
bool loadSettings();
void logData();
void calculateSoc();
void checkBatteryFull(float voltage, float current);
String getDataJSON();

// Save data to flash
void saveData() {
  File file = LittleFS.open(dataFilePath, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  
  // Write metadata
  file.write((uint8_t*)&dataIndex, sizeof(dataIndex));
  file.write((uint8_t*)&dataCount, sizeof(dataCount));
  file.write((uint8_t*)&bootTime, sizeof(bootTime));
  
  // Write data array
  file.write((uint8_t*)dataLog, sizeof(dataLog));
  
  file.close();
  Serial.println("Data saved to flash");
}

// Load data from flash
bool loadData() {
  if (!LittleFS.exists(dataFilePath)) {
    Serial.println("No saved data found");
    return false;
  }
  
  File file = LittleFS.open(dataFilePath, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return false;
  }
  
  // Read metadata
  file.read((uint8_t*)&dataIndex, sizeof(dataIndex));
  file.read((uint8_t*)&dataCount, sizeof(dataCount));
  file.read((uint8_t*)&bootTime, sizeof(bootTime));
  
  // Read data array
  file.read((uint8_t*)dataLog, sizeof(dataLog));
  
  file.close();
  
  Serial.print("Data loaded from flash: ");
  Serial.print(dataCount);
  Serial.println(" data points");
  
  return true;
}

// Save SOC data to flash
void saveSoc() {
  File file = LittleFS.open(socFilePath, "w");
  if (!file) {
    Serial.println("Failed to open SOC file for writing");
    return;
  }
  
  file.write((uint8_t*)&socPercentage, sizeof(socPercentage));
  file.write((uint8_t*)&ampHoursRemaining, sizeof(ampHoursRemaining));
  
  file.close();
}

// Load SOC data from flash
bool loadSoc() {
  if (!LittleFS.exists(socFilePath)) {
    Serial.println("No saved SOC data found - starting at 100%");
    return false;
  }
  
  File file = LittleFS.open(socFilePath, "r");
  if (!file) {
    Serial.println("Failed to open SOC file for reading");
    return false;
  }
  
  file.read((uint8_t*)&socPercentage, sizeof(socPercentage));
  file.read((uint8_t*)&ampHoursRemaining, sizeof(ampHoursRemaining));
  
  file.close();
  
  Serial.print("SOC loaded from flash: ");
  Serial.print(socPercentage, 1);
  Serial.println("%");
  
  return true;
}

// Save settings to flash
void saveSettings() {
  File file = LittleFS.open(settingsFilePath, "w");
  if (!file) {
    Serial.println("Failed to open settings file for writing");
    return;
  }
  
  file.write((uint8_t*)&batteryCapacityAh, sizeof(batteryCapacityAh));
  file.write((uint8_t*)&logIntervalMs, sizeof(logIntervalMs));
  
  file.close();
  Serial.println("Settings saved to flash");
}

// Load settings from flash
bool loadSettings() {
  if (!LittleFS.exists(settingsFilePath)) {
    Serial.println("No saved settings found - using defaults");
    return false;
  }
  
  File file = LittleFS.open(settingsFilePath, "r");
  if (!file) {
    Serial.println("Failed to open settings file for reading");
    return false;
  }
  
  file.read((uint8_t*)&batteryCapacityAh, sizeof(batteryCapacityAh));
  file.read((uint8_t*)&logIntervalMs, sizeof(logIntervalMs));
  
  file.close();
  
  Serial.print("Settings loaded - Capacity: ");
  Serial.print(batteryCapacityAh, 0);
  Serial.print("Ah, Log interval: ");
  Serial.print(logIntervalMs / 60000);
  Serial.println(" minutes");
  
  return true;
}

// Calculate SOC based on current consumption/charging
void calculateSoc() {
  unsigned long currentTime = millis();
  if (lastSocCalcTime == 0) {
    lastSocCalcTime = currentTime;
    return;
  }

  // Save SOC only when it changes significantly or periodically
  static unsigned long lastSocSaveTime = 0;
  static float lastSavedSoc = socPercentage;
  
  // Calculate time elapsed in hours
  float hoursElapsed = (currentTime - lastSocCalcTime) / 3600000.0;
  
  // Read current
  float current = ina.getCurrent_mA() / 1000.0;  // Convert to Amps
  float voltage = ina.getBusVoltage();
  
  // Calculate amp-hours consumed/charged
  float ahChange = current * hoursElapsed;
  
  // Apply Peukert correction when discharging
  if (current < 0) {  // Discharging (negative current)
    float dischargeCurrent = abs(current);
    // Peukert correction factor: (I / C20)^(n-1)
    float c20Rate = batteryCapacityAh / 20.0;
    float peukertFactor = pow(dischargeCurrent / c20Rate, PEUKERT_EXPONENT - 1.0);
    ahChange *= peukertFactor;  // Increases effective consumption at higher discharge rates
  }
  // When charging (positive current), no Peukert correction needed
  
  ampHoursRemaining += ahChange;
  
  // Clamp to battery capacity
  if (ampHoursRemaining > batteryCapacityAh) {
    ampHoursRemaining = batteryCapacityAh;
  }
  if (ampHoursRemaining < 0) {
    ampHoursRemaining = 0;
  }
  
  // Calculate percentage
  socPercentage = (ampHoursRemaining / batteryCapacityAh) * 100.0;
  
  // Check if battery is full
  checkBatteryFull(voltage, current);
  
  lastSocCalcTime = currentTime;
  
  // Save if SOC changed by >0.5% OR every 10 minutes
  bool socChanged = abs(socPercentage - lastSavedSoc) > 0.5;
  bool timeToSave = (currentTime - lastSocSaveTime >= 600000);  // 10 minutes
  
  if (socChanged || timeToSave) {
    saveSoc();
    lastSocSaveTime = currentTime;
    lastSavedSoc = socPercentage;
  }
}

// Check if battery is full and reset SOC to 100%
void checkBatteryFull(float voltage, float current) {
  // Detect full battery: voltage >= threshold AND current < threshold (nearly no charge current)
  if (voltage >= FULL_VOLTAGE_THRESHOLD && abs(current) < FULL_CURRENT_THRESHOLD) {
    if (!batteryWasFull) {
      // Start timing
      if (fullDetectionStartTime == 0) {
        fullDetectionStartTime = millis();
      }
      
      // Check if conditions held for required time
      if (millis() - fullDetectionStartTime >= FULL_DETECTION_TIME) {
        // Battery is full! Reset SOC
        socPercentage = 100.0;
        ampHoursRemaining = batteryCapacityAh;
        batteryWasFull = true;
        
        Serial.println("Battery detected as FULL - SOC reset to 100%");
        saveSoc();  // Save immediately
      }
    }
  } else {
    // Conditions not met, reset detection
    fullDetectionStartTime = 0;
    batteryWasFull = false;
  }
}

// Generate JSON string of all data points
String getDataJSON() {
  String json = "{\"data\":[";
  
  for (int i = 0; i < dataCount; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"t\":" + String(dataLog[i].timestamp) + ",";
    json += "\"v\":" + String(dataLog[i].voltage, 1) + ",";
    json += "\"c\":" + String(dataLog[i].current, 1) + ",";
    json += "\"s\":" + String(dataLog[i].soc, 1);
    json += "}";
  }
  
  json += "]}";
  return json;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("INA226 Data Logger");
  Serial.println("==================");
  
  // Initialize LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
    return;
  }
  Serial.println("LittleFS mounted successfully");
  
  // Load settings first (battery capacity, log interval)
  loadSettings();
  
  // Load data and SOC from flash
  bool dataLoaded = loadData();
  if (!loadSoc()) {
    // If no saved SOC, start at 100%
    socPercentage = 100.0;
    ampHoursRemaining = batteryCapacityAh;
  }
  
  // Initialize I2C with specified pins
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Initialize INA226
  if (!ina.begin()) {
    Serial.println("ERROR: Failed to initialize INA226!");
    Serial.println("Check connections and I2C address.");
    while (1) {
      delay(1000);
    }
  }
  
  Serial.println("INA226 initialized successfully");
  
  // Configure the INA226
  ina.setMaxCurrentShunt(50.0, SHUNT_RESISTOR);
  
  Serial.print("Shunt Resistor: ");
  Serial.print(SHUNT_RESISTOR, 4);
  Serial.println(" Ohm");
  Serial.println();
  
  // Setup WiFi Access Point
  Serial.println("Setting up WiFi Access Point...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  Serial.println("Connect to network 'Fidelio' and navigate to http://192.168.4.1");
  Serial.println();
  
  // Setup web server routes
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", getDataJSON());
  });
  
  server.on("/current", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"voltage\":" + String(ina.getBusVoltage(), 1) + ",";
    json += "\"current\":" + String(ina.getCurrent_mA() / 1000.0, 1) + ",";
    json += "\"soc\":" + String(socPercentage, 1);
    json += "}";
    request->send(200, "application/json", json);
  });
  
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"batteryCapacity\":" + String(batteryCapacityAh, 1) + ",";
    json += "\"logInterval\":" + String(logIntervalMs / 60000);  // Convert to minutes
    json += "}";
    request->send(200, "application/json", json);
  });
  
  server.on("/settings", HTTP_POST, [](AsyncWebServerRequest *request){
    bool updated = false;
    
    if (request->hasParam("batteryCapacity", true)) {
      float newCapacity = request->getParam("batteryCapacity", true)->value().toFloat();
      if (newCapacity > 0 && newCapacity <= 10000) {  // Sanity check
        batteryCapacityAh = newCapacity;
        // Reset SOC to match new capacity
        ampHoursRemaining = batteryCapacityAh * (socPercentage / 100.0);
        updated = true;
      }
    }
    
    if (request->hasParam("logInterval", true)) {
      unsigned long newInterval = request->getParam("logInterval", true)->value().toInt();
      if (newInterval >= 1 && newInterval <= 1440) {  // 1 minute to 24 hours
        logIntervalMs = newInterval * 60000;  // Convert minutes to milliseconds
        updated = true;
      }
    }
    
    if (updated) {
      saveSettings();
      saveSoc();  // Save updated ampHoursRemaining
      request->send(200, "text/plain", "Settings saved");
    } else {
      request->send(400, "text/plain", "Invalid settings");
    }
  });
  
  server.begin();
  Serial.println("Web server started");
  
  // Set bootTime - if data was loaded, it already has the original bootTime
  // Otherwise, set it to current time
  if (!dataLoaded) {
    bootTime = millis();
  }
  lastLogTime = millis() - logIntervalMs;  // Trigger immediate log on first loop
  lastSocCalcTime = millis();  // Initialize SOC calculation timer
  
  // Initialize Charlieplexed display
  display.begin();
  Serial.println("Charlieplexed 7-segment displays initialized");
  
  // Set initial display values
  display.setVoltageAndSoc(12.5, socPercentage);
  display.setCurrent(0.0);
  
  // Log first data point immediately on first boot
  if (!dataLoaded) {
    logData();
  }
}

void loop() {
  unsigned long currentTime = millis();
  static unsigned long lastDisplayBufferUpdate = 0;
  static unsigned long lastDisplayRefresh = 0;
  
  // Refresh Charlieplex display at controlled rate
  if (currentTime - lastDisplayRefresh >= REFRESH_INTERVAL_MS) {
    display.refresh();
    lastDisplayRefresh = currentTime;
  }
  
  // Update display buffer values every 500ms
  if (currentTime - lastDisplayBufferUpdate >= 500) {
    float voltage = ina.getBusVoltage();
    float current = ina.getCurrent_mA() / 1000.0;
    display.setVoltageAndSoc(voltage, socPercentage);
    display.setCurrent(current);
    lastDisplayBufferUpdate = currentTime;
  }
  
  // Calculate SOC every 10 seconds
  if (currentTime - lastSocCalcTime >= SOC_CALC_INTERVAL_MS) {
    calculateSoc();
  }
  
  // Log data at configured interval
  if (currentTime - lastLogTime >= logIntervalMs) {
    logData();
    lastLogTime = currentTime;
  }
  
  // Small delay to prevent tight loop
  delayMicroseconds(100);
}

void logData() {
  float voltage = ina.getBusVoltage();
  float current = ina.getCurrent_mA() / 1000.0;  // Convert to Amps
  
  // Calculate timestamp in minutes since boot
  unsigned long minutesSinceBoot = (millis() - bootTime) / 60000;
  
  // Store data point
  dataLog[dataIndex].timestamp = minutesSinceBoot;
  dataLog[dataIndex].voltage = voltage;
  dataLog[dataIndex].current = current;
  dataLog[dataIndex].soc = socPercentage;
  
  // Update circular buffer index
  dataIndex = (dataIndex + 1) % MAX_DATA_POINTS;
  if (dataCount < MAX_DATA_POINTS) {
    dataCount++;
  }
  
  // Save to flash
  saveData();
  
  Serial.print("Data logged - V:");
  Serial.print(voltage, 2);
  Serial.print("V I:");
  Serial.print(current, 2);
  Serial.print("A SOC:");
  Serial.print(socPercentage, 1);
  Serial.println("%");
}