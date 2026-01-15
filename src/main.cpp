#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "INA226.h"

// Test mode - set to true to load sample data instead of from filesystem
#define IS_TEST true

// INA226 I2C address (default is 0x40, verify with your module)
#define INA226_ADDRESS 0x40

// I2C pins for ESP32
#define SDA_PIN 21
#define SCL_PIN 22

// Shunt resistor value
#define SHUNT_RESISTOR 0.0015  // 0.0015 Ohm (1.5 milliohm)

// WiFi AP settings
const char* ssid = "Fidelio";
const char* password = "";  // No password

// Data logging settings
#define LOG_INTERVAL_MS (5 * 60 * 1000)  // 5 minutes in milliseconds
#define MAX_DATA_POINTS 864  // 3 days at 5-minute intervals (3*24*12)

// Data structure
struct DataPoint {
  unsigned long timestamp;  // Minutes since boot
  float voltage;
  float current;
};

DataPoint dataLog[MAX_DATA_POINTS];
int dataIndex = 0;
int dataCount = 0;
unsigned long lastLogTime = 0;
unsigned long bootTime = 0;

INA226 ina(INA226_ADDRESS);
AsyncWebServer server(80);

// File path for data storage
const char* dataFilePath = "/datalog.bin";

// Forward declarations
void saveData();
bool loadData();
void loadTestData();
void logData();
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

// Load 24 hours of test data (288 data points at 5-minute intervals)
void loadTestData() {
  Serial.println("Loading 24 hours of marine battery test data...");
  
  dataCount = 288;  // 24 hours * 12 (5-min intervals per hour)
  dataIndex = 0;
  bootTime = millis();
  
  for (int i = 0; i < dataCount; i++) {
    unsigned long minutesElapsed = i * 5;  // 5 minute intervals
    float hourOfDay = (minutesElapsed / 60.0);
    
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
  }
  
  Serial.print("Marine battery test data loaded: ");
  Serial.print(dataCount);
  Serial.println(" data points");
  Serial.println("Pattern: Night discharge → Dawn transition → Day solar charge → Dusk → Evening discharge");
}

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
      json += "\"current\":" + String(dataLog[lastIdx].current, 1);
    } else {
      json += "\"voltage\":" + String(ina.getBusVoltage(), 1) + ",";
      json += "\"current\":" + String(ina.getCurrent_mA() / 1000.0, 1);
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
  
  // Log first data point immediately on first boot (skip in test mode)
  if (!dataLoaded && !IS_TEST) {
    logData();
  }
}

void loop() {
  unsigned long currentTime = millis();
  
  // Log data every 5 minutes (skip in test mode)
  if (!IS_TEST && currentTime - lastLogTime >= LOG_INTERVAL_MS) {
    logData();
    lastLogTime = currentTime;
  }
  
  delay(100);
}

void logData() {
  float voltage = ina.getBusVoltage();
  float current = ina.getCurrent_mA() / 1000.0;  // Convert to Amps
  
  unsigned long minutesSinceBoot = (millis() - bootTime) / 60000;
  
  dataLog[dataIndex].timestamp = minutesSinceBoot;
  dataLog[dataIndex].voltage = voltage;
  dataLog[dataIndex].current = current;
  
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
  Serial.println(" A");
  
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
  
  json += "]}";
  return json;
}