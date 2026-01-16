// Charlieplexed 6-digit display driver
// Based on user's mapping data

#ifndef CHARLIEPLEX_DISPLAY_H
#define CHARLIEPLEX_DISPLAY_H

#include <Arduino.h>

// ============================================================================
// GHOSTING REDUCTION TUNING GUIDE
// ============================================================================
// Experiment with these parameters to reduce ghosting (dim segments that 
// should be off). Start with one parameter at a time.
//
// DISPLAY_BRIGHTNESS (50-500μs)
//   - Lower = dimmer & less ghosting (try 75-100 first)
//   - Higher = brighter & more ghosting
//   - Start: 100μs
//
// INTER_SEGMENT_DELAY (0-50μs)
//   - Adds delay between segments on same digit
//   - Helps capacitance discharge between segment changes
//   - Start: 10μs, increase if needed
//
// INTER_DIGIT_DELAY (0-100μs)
//   - Adds blanking time between digits
//   - Reduces crosstalk between consecutive digits
//   - Start: 20μs, increase if needed
//
// DISCHARGE_PULSE (0-20μs)
//   - Actively pulls all pins LOW between digits
//   - Discharges any capacitive buildup
//   - Start: 5μs, may help or hurt depending on circuit
//
// REVERSE_SCAN (true/false)
//   - false: Scans D1→D2→D3→D4→D5→D6 (default)
//   - true:  Scans D6→D5→D4→D3→D2→D1 (reversed)
//   - If D1 has ghosting but D6 doesn't, try reversing
//   - Start: false
//
// RECOMMENDED TROUBLESHOOTING SEQUENCE:
// 1. Set DISPLAY_BRIGHTNESS to 100 (dimmer)
// 2. Add INTER_DIGIT_DELAY 20
// 3. Try REVERSE_SCAN true (if lower digits look better)
// 4. Add INTER_SEGMENT_DELAY 10 if still ghosting
// 5. Try DISCHARGE_PULSE 5-10 as last resort
// ============================================================================

// Display brightness control (microseconds per segment)
// Lower = dimmer, higher = brighter
// Range: 50-500μs recommended
#define DISPLAY_BRIGHTNESS 100      // Start with 100μs

// Ghosting reduction parameters - experiment with these!
#define INTER_SEGMENT_DELAY 0     // μs delay between segments on same digit (0-50)
#define INTER_DIGIT_DELAY 30       // μs delay between digits (0-100) - START HERE
#define DISCHARGE_PULSE 0         // μs to actively discharge pins between digits (0-20)
#define REVERSE_SCAN false         // true = scan D6→D1 instead of D1→D6

#define FLASH_INTERVAL_MS 400 

// SELECT YOUR SCAN SEQUENCE HERE:
#define SCAN_SEQUENCE_NORMAL    {0,1,2,3,4,5}  // D1→D2→D3→D4→D5→D6 (D1 ghosts)
#define SCAN_SEQUENCE_REVERSE   {5,4,3,2,1,0}  // D6→D5→D4→D3→D2→D1 (D4 ghosts)
#define SCAN_SEQUENCE_INTERLEAVE {1,4,0,3,2,5} // D2→D5→D1→D4→D3→D6 (try this!)
#define SCAN_SEQUENCE_SKIP      {0,2,4,1,3,5}  // D1→D3→D5→D2→D4→D6
#define SCAN_SEQUENCE_DP_FIRST  {1,4,2,5,0,3}  // D2→D5→D3→D6→D1→D4 (DP digits first)
#define SCAN_SEQUENCE_ENDS_LAST {2,5,1,4,0,3}  // D3→D6→D2→D5→D1→D4 (problematic ends last)

const uint8_t scanSequence[6] = SCAN_SEQUENCE_REVERSE;  // Start with DP_FIRST

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
  0b00000000   // 12 = Off
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
  bool ghostingTestMode;  // If true, run ghosting test instead of digit cycle
  int lastTestIndex;  // Track last test index for serial output
  uint8_t toggleState = 0; // For flashing when negative
  
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
    lastUpdate = 0;
    testMode = false;
    testModeStartTime = 0;
    testDigitValue = 0;
    ghostingTestMode = false;
    lastTestIndex = -1;
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
    ghostingTestMode = false;
    testModeStartTime = millis();
    testDigitValue = 0;
  }
  
  void startGhostingTest() {
    testMode = true;
    ghostingTestMode = true;
    testModeStartTime = millis();
    lastTestIndex = -1;  // Reset to ensure first value prints
  }
  
  void stopTestPattern() {
    testMode = false;
    ghostingTestMode = false;
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
    bool charging = (current > 0);
    float absCurrent = abs(current);

    unsigned long currentTime = millis();
    static unsigned long lastDisplayFlash = 0;
    
    if (currentTime - lastDisplayFlash >= FLASH_INTERVAL_MS) {
        if (toggleState == 0) {
            toggleState = 1;
        } else {
            toggleState = 0;
        }
        lastDisplayFlash = currentTime;
    }

    int currentInt = (int)(absCurrent * 10.0 + 0.5);
    if (currentInt > 99) currentInt = 99;

    // Flashing DP when charging
    if (charging) {
        if (toggleState == 1) {
            decimalPoints[4] = false;   // DP after ones
        } else {
            decimalPoints[4] = true;   // DP after ones
        }
    } else {
        decimalPoints[4] = true;
    }

    // if (currentInt <= 10 || currentInt >= 10) {
    //     displayBuffer[3] = 12; // First digit off
    // } else {
        displayBuffer[3] = (currentInt / 100) % 10;
    // }
    
    displayBuffer[4] = (currentInt / 10) % 10;
    displayBuffer[5] = currentInt % 10;

    decimalPoints[3] = false;      
    decimalPoints[5] = false;
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
  
  void updateGhostingTest() {
    // Hold voltage at 12.3V and cycle current from -2.0A to +2.0A
    unsigned long elapsed = millis() - testModeStartTime;
    
    // Cycle every 2 seconds through different current values
    // -2.0 to +2.0 in 0.1A steps = 41 values
    int testIndex = (elapsed / 2000) % 41;
    
    // Calculate current: -2.0 + (testIndex * 0.1)
    float testCurrent = -2.0 + (testIndex * 0.1);
    
    // Print to serial when value changes
    if (testIndex != lastTestIndex) {
      Serial.print("Ghosting Test - Voltage: 12.3V, Current: ");
      if (testCurrent >= 0) {
        Serial.print("+");
      }
      Serial.print(testCurrent, 1);
      Serial.println("A");
      lastTestIndex = testIndex;
    }
    
    // Set fixed voltage
    setVoltage(12.3);
    
    // Set cycling current
    setCurrent(testCurrent);
  }
  
void refresh() {
    // Update test pattern if in test mode
    if (testMode) {
      if (ghostingTestMode) {
        updateGhostingTest();
      } else {
        updateTestPattern();
      }
    }
    
    // Use custom scan sequence
    uint8_t displayDigit = scanSequence[currentDigit];

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
        
        // EXTRA DISCHARGE between segments that share pins (especially D4)
        setAllPinsHighZ();
        delayMicroseconds(15);  // Let shared pins fully discharge
        
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