// Charlieplexed 6-digit display driver
#ifndef CHARLIEPLEX_DISPLAY_H
#define CHARLIEPLEX_DISPLAY_H

#include <Arduino.h>

// Display brightness control (microseconds per segment)
#define DISPLAY_BRIGHTNESS 100

// Ghosting reduction parameters
#define INTER_SEGMENT_DELAY 0      // μs delay between segments on same digit (0-50)
#define INTER_DIGIT_DELAY 30       // μs delay between digits (0-100)
#define DISCHARGE_PULSE 0          // μs to actively discharge pins between digits (0-20)
#define REVERSE_SCAN true          // true = scan D6→D1 instead of D1→D6

// Flash interval for charging indicator (milliseconds)
#define FLASH_INTERVAL_MS 400

// Display pin definitions
#define CPIN0 33
#define CPIN1 25
#define CPIN2 4
#define CPIN3 16
#define CPIN4 17
#define CPIN5 32
#define CPIN6 18
#define CPIN7 19
#define CPIN8 23

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
  {{2,1}, {3,1}, {4,1}, {5,1}, {6,1}, {7,1}, {8,1}, {5,4}},
  // D5 (Current ones)
  {{2,3}, {2,4}, {2,5}, {2,6}, {2,7}, {2,8}, {4,6}, {4,7}},
  // D6 (Current tenths/ones)
  {{3,2}, {4,2}, {5,2}, {6,2}, {7,2}, {8,2}, {6,4}, {7,4}}
};

// 7-segment digit patterns (which segments to light for 0-9)
// Bits: DP G F E D C B A
const uint8_t digitPatterns[13] = {
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
  0b01000000,  // 10 = minus sign (G segment)
  0b00001000,  // 11 = underscore (D segment)
  0b00000000   // 12 = blank
};

class CharlieplexDisplay {
private:
  uint8_t displayBuffer[6];     // What to show on each digit (0-9, 10=minus, 11=underscore, 12=blank)
  bool decimalPoints[6];        // Decimal point for each digit
  uint8_t currentDigit;
  uint8_t toggleState;          // For flashing DP when charging
  unsigned long lastFlashTime;
  
  void setAllPinsHighZ() {
    // Set all pins to true high impedance (no pull-ups/downs)
    for (int i = 0; i < 9; i++) {
      pinMode(charliePins[i], INPUT);
    }
  }
  
  void dischargeAllPins() {
    // Actively discharge all pins to GND briefly
    if (DISCHARGE_PULSE > 0) {
      for (int i = 0; i < 9; i++) {
        pinMode(charliePins[i], OUTPUT);
        digitalWrite(charliePins[i], LOW);
      }
      delayMicroseconds(DISCHARGE_PULSE);
      setAllPinsHighZ();
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
    toggleState = 0;
    lastFlashTime = 0;
    for (int i = 0; i < 6; i++) {
      displayBuffer[i] = 0;
      decimalPoints[i] = false;
    }
  }
  
  void begin() {
    setAllPinsHighZ();
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
    bool charging = (current > 0);
    float absCurrent = abs(current);

    unsigned long currentTime = millis();
    
    // Toggle flash state every FLASH_INTERVAL_MS
    if (currentTime - lastFlashTime >= FLASH_INTERVAL_MS) {
      toggleState = 1 - toggleState;
      lastFlashTime = currentTime;
    }

    int currentInt = (int)(absCurrent * 10.0 + 0.5);
    if (currentInt > 99) currentInt = 99;

    // Flash DP when charging, solid when discharging
    if (charging) {
      decimalPoints[4] = (toggleState == 1);
    } else {
      decimalPoints[4] = true;
    }

    displayBuffer[3] = (currentInt / 100) % 10;
    displayBuffer[4] = (currentInt / 10) % 10;
    displayBuffer[5] = currentInt % 10;

    decimalPoints[3] = false;
    decimalPoints[5] = false;
  }
  
  void refresh() {
    // Determine which digit to display (support reverse scanning)
    uint8_t displayDigit;
    if (REVERSE_SCAN) {
      displayDigit = 5 - currentDigit;  // Scan D6→D1
    } else {
      displayDigit = currentDigit;      // Scan D1→D6
    }
    
    // Get pattern for current digit
    uint8_t pattern = digitPatterns[displayBuffer[displayDigit]];
    
    // Light each segment that should be on
    for (int seg = 0; seg < 8; seg++) {
      bool shouldLight = false;
      
      if (seg == 7) {  // Decimal point
        shouldLight = decimalPoints[displayDigit];
      } else {  // Regular segment
        shouldLight = (pattern & (1 << seg)) != 0;
      }
      
      if (shouldLight) {
        uint8_t anode = digitMap[displayDigit][seg][0];
        uint8_t cathode = digitMap[displayDigit][seg][1];
        
        // Extra discharge between segments to prevent ghosting on shared pins
        setAllPinsHighZ();
        delayMicroseconds(15);
        
        lightSegment(anode, cathode);
        delayMicroseconds(DISPLAY_BRIGHTNESS);
        
        // Turn off immediately
        setAllPinsHighZ();
        
        // Optional inter-segment delay
        if (INTER_SEGMENT_DELAY > 0) {
          delayMicroseconds(INTER_SEGMENT_DELAY);
        }
      }
    }
    
    // Turn off all pins before moving to next digit
    setAllPinsHighZ();
    
    // Optional discharge pulse
    dischargeAllPins();
    
    // Optional inter-digit delay
    if (INTER_DIGIT_DELAY > 0) {
      delayMicroseconds(INTER_DIGIT_DELAY);
    }
    
    // Move to next digit
    currentDigit = (currentDigit + 1) % 6;
  }
};

#endif