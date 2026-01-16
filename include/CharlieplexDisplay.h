// Charlieplexed 6-digit display driver
// Based on user's mapping data

#ifndef CHARLIEPLEX_DISPLAY_H
#define CHARLIEPLEX_DISPLAY_H

#include <Arduino.h>

// Display pin definitions
#define CPIN0 23
#define CPIN1 19
#define CPIN2 18
#define CPIN3 5
#define CPIN4 17
#define CPIN5 16
#define CPIN6 4
#define CPIN7 2
#define CPIN8 15

const uint8_t charliePins[] = {CPIN0, CPIN1, CPIN2, CPIN3, CPIN4, CPIN5, CPIN6, CPIN7, CPIN8};

// Segment mapping for each digit
// Format: {anode_pin, cathode_pin} for segments A, B, C, D, E, F, G, DP
const uint8_t digitMap[6][8][2] = {
  // D1 (Voltage tens)
  {{0,1}, {0,2}, {0,3}, {0,4}, {0,5}, {0,6}, {0,7}, {0,8}},
  // D2 (Voltage ones)
  {{1,0}, {2,0}, {3,0}, {4,0}, {5,0}, {6,0}, {7,0}, {8,0}},
  // D3 (Voltage tenths)
  {{1,2}, {1,3}, {1,4}, {1,5}, {1,6}, {1,7}, {1,8}, {4,5}},
  // D4 (Current sign/tens)
  {{2,1}, {255,255}, {4,1}, {5,1}, {255,255}, {7,1}, {8,1}, {5,4}},  // B and E missing
  // D5 (Current ones)
  {{2,3}, {2,4}, {2,5}, {2,6}, {2,7}, {2,8}, {4,6}, {4,7}},
  // D6 (Current tenths/ones)
  {{3,2}, {4,2}, {5,2}, {6,2}, {7,2}, {8,2}, {6,4}, {7,4}}
};

// 7-segment digit patterns (which segments to light for 0-9)
// Bits: DP G F E D C B A
const uint8_t digitPatterns[11] = {
  0b00111111,  // 0
  0b00000110,  // 1
  0b01011011,  // 2
  0b01001111,  // 3
  0b01100110,  // 4
  0b01101101,  // 5
  0b01111101,  // 6
  0b00000111,  // 7
  0b01111111,  // 8
  0b01101111,  // 9
  0b01000000   // 10 = minus sign (G segment only)
};

class CharlieplexDisplay {
private:
  uint8_t displayBuffer[6];  // What to show on each digit (0-9 or 10 for minus)
  bool decimalPoints[6];     // Decimal point for each digit
  uint8_t currentDigit;
  unsigned long lastUpdate;
  bool testMode;
  unsigned long testModeStartTime;
  uint8_t testDigitValue;
  
  void setAllPinsHighZ() {
    // Set all pins to true high impedance (no pull-ups/downs)
    for (int i = 0; i < 9; i++) {
      pinMode(charliePins[i], INPUT);
    }
  }
  
  void lightSegment(uint8_t anode, uint8_t cathode) {
    if (anode == 255 || cathode == 255) return;  // Invalid mapping
    
    // First turn everything off to prevent crosstalk
    setAllPinsHighZ();
    
    // Now set only the two pins we need
    pinMode(charliePins[anode], OUTPUT);
    pinMode(charliePins[cathode], OUTPUT);
    digitalWrite(charliePins[anode], HIGH);
    digitalWrite(charliePins[cathode], LOW);
  }
  
public:
  CharlieplexDisplay() {
    currentDigit = 0;
    lastUpdate = 0;
    testMode = false;
    testModeStartTime = 0;
    testDigitValue = 0;
    for (int i = 0; i < 6; i++) {
      displayBuffer[i] = 0;
      decimalPoints[i] = false;
    }
  }
  
  void begin() {
    setAllPinsHighZ();
  }
  
  void startTestPattern() {
    testMode = true;
    testModeStartTime = millis();
    testDigitValue = 0;
  }
  
  void stopTestPattern() {
    testMode = false;
  }
  
  bool isTestMode() {
    return testMode;
  }
  
  void setDigit(uint8_t digit, uint8_t value, bool dp = false) {
    if (digit < 6) {
      displayBuffer[digit] = value;
      decimalPoints[digit] = dp;
    }
  }
  
  void setVoltage(float voltage) {
    // Format as ##.# (always 1 decimal place)
    int voltageInt = (int)(voltage * 10.0 + 0.5);
    if (voltageInt > 999) voltageInt = 999;
    if (voltageInt < 0) voltageInt = 0;
    
    displayBuffer[0] = (voltageInt / 100) % 10;   // Tens
    displayBuffer[1] = (voltageInt / 10) % 10;    // Ones
    displayBuffer[2] = voltageInt % 10;           // Tenths
    
    decimalPoints[0] = false;
    decimalPoints[1] = true;   // DP after ones
    decimalPoints[2] = false;
  }
  
  void setCurrent(float current) {
    // Format with adaptive decimal point
    bool showDP = (current >= -9.9 && current <= 9.9);
    bool negative = (current < 0);
    float absCurrent = abs(current);
    
    if (showDP) {
      // Show with decimal point: -##.#
      int currentInt = (int)(absCurrent * 10.0 + 0.5);
      if (currentInt > 99) currentInt = 99;
      
      if (negative) {
        displayBuffer[3] = 10;  // Minus sign
      } else {
        displayBuffer[3] = (currentInt / 100) % 10;
      }
      displayBuffer[4] = (currentInt / 10) % 10;
      displayBuffer[5] = currentInt % 10;
      
      decimalPoints[3] = false;
      decimalPoints[4] = true;   // DP after ones
      decimalPoints[5] = false;
    } else {
      // No decimal point: -###
      int currentInt = (int)(absCurrent + 0.5);
      if (currentInt > 999) currentInt = 999;
      
      if (negative) {
        displayBuffer[3] = 10;  // Minus sign
        displayBuffer[4] = (currentInt / 10) % 10;
        displayBuffer[5] = currentInt % 10;
      } else {
        displayBuffer[3] = (currentInt / 100) % 10;
        displayBuffer[4] = (currentInt / 10) % 10;
        displayBuffer[5] = currentInt % 10;
      }
      
      decimalPoints[3] = false;
      decimalPoints[4] = false;
      decimalPoints[5] = false;
    }
  }
  
  void updateTestPattern() {
    // Cycle through digits 0-9 every second, with DP on
    unsigned long elapsed = millis() - testModeStartTime;
    testDigitValue = (elapsed / 1000) % 10;  // Change every second
    
    // Show same digit on all positions
    for (int i = 0; i < 6; i++) {
      displayBuffer[i] = testDigitValue;
      decimalPoints[i] = true;  // Show all decimal points
    }
  }
  
  void refresh() {
    // Update test pattern if in test mode
    if (testMode) {
      updateTestPattern();
    }
    
    // Get pattern for current digit
    uint8_t pattern = digitPatterns[displayBuffer[currentDigit]];
    
    // Light each segment that should be on
    for (int seg = 0; seg < 8; seg++) {
      bool shouldLight = false;
      
      if (seg == 7) {  // Decimal point
        shouldLight = decimalPoints[currentDigit];
      } else {  // Regular segment
        shouldLight = (pattern & (1 << seg)) != 0;
      }
      
      if (shouldLight) {
        uint8_t anode = digitMap[currentDigit][seg][0];
        uint8_t cathode = digitMap[currentDigit][seg][1];
        lightSegment(anode, cathode);
        delayMicroseconds(200);  // Light time per segment
      }
    }
    
    // Turn off all pins before moving to next digit
    setAllPinsHighZ();
    
    // Move to next digit
    currentDigit = (currentDigit + 1) % 6;
  }
};

#endif