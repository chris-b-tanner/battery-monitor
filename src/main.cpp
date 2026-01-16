#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "INA226.h"
#include "CharlieplexDisplay.h"

// Test mode - set to true to load sample data instead of from filesystem
#define IS_TEST true

// INA226 I2C address (default is 0x40, verify with your module)
#define INA226_ADDRESS 0x40

// I2C pins for ESP32
#define SDA_PIN 21
#define SCL_PIN 22

// Shunt resistor value
#define SHUNT_RESISTOR 0.0015  // 0.0015 Ohm (1.5 milliohm)

// Battery specifications
#define BATTERY_CAPACITY_AH 300.0  // 300Ah battery bank
#define PEUKERT_EXPONENT 1.1       // Peukert exponent for lead acid (typically 1.05-1.4)
#define C20_RATE 15.0              // C20 rate in amps (300Ah / 20h = 15A)
#define FULL_VOLTAGE_THRESHOLD 13.8  // Voltage threshold for "full" detection
#define FULL_CURRENT_THRESHOLD 1.0   // Current below this (in A) indicates full when voltage high
#define FULL_DETECTION_TIME 60000    // Must meet criteria for 60 seconds

// SOC calculation settings
#define SOC_CALC_INTERVAL_MS 10000  // Calculate SOC every 10 seconds

// WiFi AP settings
const char* ssid = "Fidelio";
const char* password = "";  // No password

// Data logging settings
#define LOG_INTERVAL_MS (10 * 60 * 1000)  // 10 minutes in milliseconds
#define MAX_DATA_POINTS 288  // 48 hours at 10-minute intervals (48*6)

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
float ampHoursRemaining = BATTERY_CAPACITY_AH;  // Amp-hours remaining
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

// Forward declarations
void saveData();
bool loadData();
void saveSoc();
bool loadSoc();
void loadTestData();
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
  Serial.print("% (");
  Serial.print(ampHoursRemaining, 1);
  Serial.println(" Ah remaining)");
  
  return true;
}

// Check if battery is full and reset SOC to 100%
void checkBatteryFull(float voltage, float current) {
  // Battery is considered full when voltage > threshold AND current < threshold
  if (voltage >= FULL_VOLTAGE_THRESHOLD && abs(current) < FULL_CURRENT_THRESHOLD) {
    if (fullDetectionStartTime == 0) {
      fullDetectionStartTime = millis();
    } else if (millis() - fullDetectionStartTime >= FULL_DETECTION_TIME) {
      // Conditions met for required duration
      if (!batteryWasFull) {
        socPercentage = 100.0;
        ampHoursRemaining = BATTERY_CAPACITY_AH;
        batteryWasFull = true;
        saveSoc();
        Serial.println("Battery detected as FULL - SOC reset to 100%");
      }
    }
  } else {
    fullDetectionStartTime = 0;
    batteryWasFull = false;
  }
}

// Calculate SOC based on current consumption/charging
void calculateSoc() {
  if (IS_TEST) return;  // Skip in test mode
  
  unsigned long currentTime = millis();
  if (lastSocCalcTime == 0) {
    lastSocCalcTime = currentTime;
    return;
  }
  
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
    float peukertFactor = pow(dischargeCurrent / C20_RATE, PEUKERT_EXPONENT - 1.0);
    ahChange *= peukertFactor;  // Increases effective consumption at higher discharge rates
  }
  // When charging (positive current), no Peukert correction needed
  
  ampHoursRemaining += ahChange;
  
  // Clamp to battery capacity
  if (ampHoursRemaining > BATTERY_CAPACITY_AH) {
    ampHoursRemaining = BATTERY_CAPACITY_AH;
  }
  if (ampHoursRemaining < 0) {
    ampHoursRemaining = 0;
  }
  
  // Calculate percentage
  socPercentage = (ampHoursRemaining / BATTERY_CAPACITY_AH) * 100.0;
  
  // Check if battery is full
  checkBatteryFull(voltage, current);
  
  lastSocCalcTime = currentTime;
  
  // Save SOC every minute
  static unsigned long lastSocSaveTime = 0;
  if (currentTime - lastSocSaveTime >= 60000) {
    saveSoc();
    lastSocSaveTime = currentTime;
  }
}

// Load 48 hours of test data (288 data points at 10-minute intervals)
void loadTestData() {
  Serial.println("Loading 48 hours of marine battery test data...");
  
  dataCount = 288;  // 48 hours * 6 (10-min intervals per hour)
  dataIndex = 0;
  bootTime = millis();
  
  for (int i = 0; i < dataCount; i++) {
    unsigned long minutesElapsed = i * 10;  // 10 minute intervals
    float hourOfDay = fmod(minutesElapsed / 60.0, 24.0);  // Wrap to 24-hour cycle
    
    // Generate realistic marine battery voltage and current patterns
    float voltage, current;
    
    if (hourOfDay < 6) {
      // Night: 0-6am - discharge (negative current), voltage drops
      float nightProgress = hourOfDay / 6.0;  // 0 to 1
      voltage = 12.5 - (nightProgress * 0.7);  // 12.5V down to 11.8V
      voltage += (sin(hourOfDay * 2) * 0.1) + ((random(-50, 50)) / 500.0);
      current = -3.0 - (sin(hourOfDay * 1.5) * 2.0) + ((random(-100, 100)) / 100.0);
      
    } else if (hourOfDay < 8) {
      // Dawn: 6-8am - transition to charging
      float dawnProgress = (hourOfDay - 6) / 2.0;  // 0 to 1
      voltage = 11.8 + (dawnProgress * 0.7);  // 11.8V up to 12.5V
      voltage += ((random(-50, 50)) / 500.0);
      current = -2.0 + (dawnProgress * 4.0) + ((random(-50, 50)) / 100.0);
      
    } else if (hourOfDay < 16) {
      // Day: 8am-4pm - solar charging (positive current), voltage rises
      float dayProgress = (hourOfDay - 8) / 8.0;  // 0 to 1
      float solarIntensity = sin(dayProgress * 3.14159);  // Bell curve for sun
      
      voltage = 12.5 + (solarIntensity * 1.5);  // 12.5V up to 14.0V at peak
      voltage += ((random(-50, 50)) / 500.0);
      
      current = 5.0 + (solarIntensity * 8.0);  // 5A to 13A at solar peak
      current += ((random(-100, 100)) / 100.0);
      
    } else if (hourOfDay < 18) {
      // Dusk: 4-6pm - decreasing solar charge
      float duskProgress = (hourOfDay - 16) / 2.0;  // 0 to 1
      voltage = 13.8 - (duskProgress * 1.0);  // 13.8V down to 12.8V
      voltage += ((random(-50, 50)) / 500.0);
      current = 8.0 - (duskProgress * 9.0) + ((random(-50, 50)) / 100.0);
      
    } else {
      // Evening: 6pm-midnight - discharge resumes (negative current)
      float eveningProgress = (hourOfDay - 18) / 6.0;  // 0 to 1
      voltage = 12.8 - (eveningProgress * 0.5);  // 12.8V down to 12.3V
      voltage += (sin(hourOfDay * 2) * 0.1) + ((random(-50, 50)) / 500.0);
      current = -1.5 - (eveningProgress * 2.5) + ((random(-100, 100)) / 100.0);
    }
    
    // Clamp voltage to reasonable range
    if (voltage < 11.0) voltage = 11.0;
    if (voltage > 14.5) voltage = 14.5;
    
    dataLog[i].timestamp = minutesElapsed;
    dataLog[i].voltage = voltage;
    dataLog[i].current = current;
    
    // Calculate SOC for test data
    // Start at 100% and integrate current over time
    if (i == 0) {
      dataLog[i].soc = 100.0;
      ampHoursRemaining = BATTERY_CAPACITY_AH;
    } else {
      // Calculate Ah consumed/charged in this 10-minute interval
      float hoursElapsed = 10.0 / 60.0;  // 10 minutes = 0.1667 hours
      float ahChange = current * hoursElapsed;
      
      // Apply Peukert correction when discharging
      if (current < 0) {  // Discharging
        float dischargeCurrent = abs(current);
        float peukertFactor = pow(dischargeCurrent / C20_RATE, PEUKERT_EXPONENT - 1.0);
        ahChange *= peukertFactor;
      }
      
      ampHoursRemaining += ahChange;
      
      // Clamp to capacity
      if (ampHoursRemaining > BATTERY_CAPACITY_AH) ampHoursRemaining = BATTERY_CAPACITY_AH;
      if (ampHoursRemaining < 0) ampHoursRemaining = 0;
      
      dataLog[i].soc = (ampHoursRemaining / BATTERY_CAPACITY_AH) * 100.0;
    }
  }
  
  // Set current SOC to last data point
  socPercentage = dataLog[dataCount - 1].soc;
  
  Serial.print("Marine battery test data loaded: ");
  Serial.print(dataCount);
  Serial.println(" data points (48 hours, 10 minute intervals)");
  Serial.println("Pattern: Night discharge → Dawn transition → Day solar charge → Dusk → Evening discharge (repeating)");
}

// Initialize 7-segment display pins
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("INA226 Data Logger");
  Serial.println("==================");
  
  if (IS_TEST) {
    Serial.println("*** TEST MODE ENABLED ***");
    Serial.println("Using sample data instead of sensor readings");
  }
  
  // Initialize LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
    return;
  }
  Serial.println("LittleFS mounted successfully");
  
  // Load data based on test mode
  bool dataLoaded = false;
  if (IS_TEST) {
    loadTestData();
    dataLoaded = true;
  } else {
    dataLoaded = loadData();
    // Load SOC data
    if (!loadSoc()) {
      // If no saved SOC, start at 100%
      socPercentage = 100.0;
      ampHoursRemaining = BATTERY_CAPACITY_AH;
    }
  }
  
  // Initialize I2C with specified pins
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Initialize INA226 (skip in test mode)
  if (!IS_TEST) {
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
  }
  
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
    if (IS_TEST) {
      // Return last logged test data point
      int lastIdx = (dataIndex > 0) ? dataIndex - 1 : dataCount - 1;
      json += "\"voltage\":" + String(dataLog[lastIdx].voltage, 1) + ",";
      json += "\"current\":" + String(dataLog[lastIdx].current, 1) + ",";
      json += "\"soc\":" + String(dataLog[lastIdx].soc, 1);
    } else {
      json += "\"voltage\":" + String(ina.getBusVoltage(), 1) + ",";
      json += "\"current\":" + String(ina.getCurrent_mA() / 1000.0, 1) + ",";
      json += "\"soc\":" + String(socPercentage, 1);
    }
    json += "}";
    request->send(200, "application/json", json);
  });
  
  server.begin();
  Serial.println("Web server started");
  
  // Set bootTime - if data was loaded, it already has the original bootTime
  // Otherwise, set it to current time
  if (!dataLoaded) {
    bootTime = millis();
  }
  lastLogTime = millis() - LOG_INTERVAL_MS;  // Trigger immediate log on first loop
  lastSocCalcTime = millis();  // Initialize SOC calculation timer
  
  // Initialize Charlieplexed display
  display.begin();
  Serial.println("Charlieplexed 7-segment displays initialized");
  
  // In test mode, start with ghosting test for 2 minutes
  if (IS_TEST) {
    display.startGhostingTest();
    Serial.println("GHOSTING TEST MODE - 82 seconds (41 values × 2s each)");
    Serial.println("Voltage: 12.3V (constant)");
    Serial.println("Current: Cycling -2.0A to +2.0A in 0.1A steps");
    Serial.println("Watch for ghosting on D1 segment A (top bar of voltage tens digit)");
    Serial.println("Each value displayed for 2 seconds");
  }
  
  // Set initial display values (will be used after test pattern ends)
  if (IS_TEST) {
    int lastIdx = (dataIndex > 0) ? dataIndex - 1 : dataCount - 1;
    display.setVoltage(dataLog[lastIdx].voltage);
    display.setCurrent(dataLog[lastIdx].current);
  } else {
    display.setVoltage(12.5);
    display.setCurrent(0.0);
  }
  
  // Log first data point immediately on first boot (skip in test mode)
  if (!dataLoaded && !IS_TEST) {
    logData();
  }
}

void loop() {
  unsigned long currentTime = millis();
  static unsigned long lastDisplayBufferUpdate = 0;
  static unsigned long lastDisplayRefresh = 0;  
  static bool testPatternStopped = false;
  
  // Stop test pattern after test completes (82 seconds for 41 values)
  if (IS_TEST && display.isTestMode() && !testPatternStopped && currentTime > 90000) {
    display.stopTestPattern();
    testPatternStopped = true;
    Serial.println("Ghosting test complete - showing battery data");
    
    // Update display with current values
    int lastIdx = (dataIndex > 0) ? dataIndex - 1 : dataCount - 1;
    display.setVoltage(dataLog[lastIdx].voltage);
    display.setCurrent(dataLog[lastIdx].current);
  }
  
  // REPLACE: Refresh Charlieplex display at controlled rate
  // Try values: 0 (fastest), 1, 2, 5, 10 milliseconds
  #define REFRESH_INTERVAL_MS 0  // Start with 0, increase if ghosting
  
  if (currentTime - lastDisplayRefresh >= REFRESH_INTERVAL_MS) {
    display.refresh();
    lastDisplayRefresh = currentTime;
  }
  
  // Update display buffer values every 500ms (skip if in test pattern mode)
  if (!display.isTestMode() && currentTime - lastDisplayBufferUpdate >= 500) {
    if (IS_TEST) {
      int lastIdx = (dataIndex > 0) ? dataIndex - 1 : dataCount - 1;
      display.setVoltage(dataLog[lastIdx].voltage);
      display.setCurrent(dataLog[lastIdx].current);
    } else {
      float voltage = ina.getBusVoltage();
      float current = ina.getCurrent_mA() / 1000.0;
      display.setVoltage(voltage);
      display.setCurrent(current);
    }
    lastDisplayBufferUpdate = currentTime;
  }
  
  // Calculate SOC every 10 seconds (only in real mode)
  if (!IS_TEST && currentTime - lastSocCalcTime >= SOC_CALC_INTERVAL_MS) {
    calculateSoc();
  }
  
  // Log data every 10 minutes (skip in test mode)
  if (!IS_TEST && currentTime - lastLogTime >= LOG_INTERVAL_MS) {
    logData();
    lastLogTime = currentTime;
  }
  
  // Small delay to prevent tight loop
  delayMicroseconds(100);
}

void logData() {
  float voltage = ina.getBusVoltage();
  float current = ina.getCurrent_mA() / 1000.0;  // Convert to Amps
  
  unsigned long minutesSinceBoot = (millis() - bootTime) / 60000;
  
  dataLog[dataIndex].timestamp = minutesSinceBoot;
  dataLog[dataIndex].voltage = voltage;
  dataLog[dataIndex].current = current;
  dataLog[dataIndex].soc = socPercentage;
  
  dataIndex = (dataIndex + 1) % MAX_DATA_POINTS;
  if (dataCount < MAX_DATA_POINTS) {
    dataCount++;
  }
  
  Serial.print("Logged: ");
  Serial.print(minutesSinceBoot);
  Serial.print(" min, ");
  Serial.print(voltage, 1);
  Serial.print(" V, ");
  Serial.print(current, 1);
  Serial.print(" A, SOC: ");
  Serial.print(socPercentage, 1);
  Serial.println(" %");
  
  // Save to flash after each log (only if not in test mode)
  if (!IS_TEST) {
    saveData();
  }
}

String getDataJSON() {
  String json = "{\"timestamps\":[";
  
  // Calculate starting index for chronological order
  int startIdx = (dataCount < MAX_DATA_POINTS) ? 0 : dataIndex;
  
  for (int i = 0; i < dataCount; i++) {
    int idx = (startIdx + i) % MAX_DATA_POINTS;
    if (i > 0) json += ",";
    json += String(dataLog[idx].timestamp);
  }
  
  json += "],\"voltages\":[";
  for (int i = 0; i < dataCount; i++) {
    int idx = (startIdx + i) % MAX_DATA_POINTS;
    if (i > 0) json += ",";
    json += String(dataLog[idx].voltage, 1);
  }
  
  json += "],\"currents\":[";
  for (int i = 0; i < dataCount; i++) {
    int idx = (startIdx + i) % MAX_DATA_POINTS;
    if (i > 0) json += ",";
    json += String(dataLog[idx].current, 1);
  }
  
  json += "],\"soc\":[";
  for (int i = 0; i < dataCount; i++) {
    int idx = (startIdx + i) % MAX_DATA_POINTS;
    if (i > 0) json += ",";
    json += String(dataLog[idx].soc, 1);
  }
  
  json += "]}";
  return json;
}