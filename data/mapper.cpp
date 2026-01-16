#include <Arduino.h>

// Define the 9 pins you'll connect the display to
#define PIN0 33
#define PIN1 25
#define PIN2 4
#define PIN3 17
#define PIN4 16
#define PIN5 32
#define PIN6 18
#define PIN7 19
#define PIN8 23

// Button for manual progression
#define BUTTON_PIN 27

const uint8_t displayPins[] = {PIN0, PIN1, PIN2, PIN3, PIN4, PIN5, PIN6, PIN7, PIN8};
const int numPins = 9;

bool waitForButtonPress() {
  delay(200);
  return true;

  // Wait for button to be pressed (LOW)
  while (digitalRead(BUTTON_PIN) == HIGH) {
    delay(10);
  }
  
  // Debounce - wait for stable LOW
  delay(50);
  
  // Wait for button to be released (HIGH)
  while (digitalRead(BUTTON_PIN) == LOW) {
    delay(10);
  }
  
  // Debounce - wait for stable HIGH
  delay(50);
  
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Setup button with internal pullup
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  Serial.println("6-Digit Display Mapper (2 rows x 3 digits)");
  Serial.println("===========================================");
  Serial.println();
  Serial.println("Display layout:");
  Serial.println("  Top row:    [Digit 1] [Digit 2] [Digit 3]");
  Serial.println("  Bottom row: [Digit 4] [Digit 5] [Digit 6]");
  Serial.println();
  Serial.println("Segment names (standard 7-segment):");
  Serial.println("     AAA");
  Serial.println("    F   B");
  Serial.println("     GGG");
  Serial.println("    E   C");
  Serial.println("     DDD  DP");
  Serial.println();
  Serial.println("For each test, record:");
  Serial.println("  - Which digit (1-6)");
  Serial.println("  - Which segment (A, B, C, D, E, F, G, or DP)");
  Serial.println("  - Or 'NONE' if nothing lights");
  Serial.println();
  Serial.println("Button: Press to advance to next test");
  Serial.println();
  Serial.println("Press button to start...");
  
  waitForButtonPress();
  
  // Set all pins to input (high impedance) initially
  for (int i = 0; i < numPins; i++) {
    pinMode(displayPins[i], INPUT);
  }
}

void loop() {
  static int testNumber = 1;
  
  // Test each possible pin pair in both directions
  for (int anode = 0; anode < numPins; anode++) {
    for (int cathode = 0; cathode < numPins; cathode++) {
      if (anode == cathode) continue;  // Skip same pin
      
      // Set all other pins to high impedance
      for (int i = 0; i < numPins; i++) {
        pinMode(displayPins[i], INPUT);
      }
      
      // Set anode HIGH and cathode LOW
      pinMode(displayPins[anode], OUTPUT);
      pinMode(displayPins[cathode], OUTPUT);
      digitalWrite(displayPins[anode], HIGH);
      digitalWrite(displayPins[cathode], LOW);
      
      // Display which combination we're testing
      Serial.println("=========================================");
      Serial.print("Test #");
      Serial.print(testNumber++);
      Serial.print(": Pin");
      Serial.print(anode);
      Serial.print("(GPIO");
      Serial.print(displayPins[anode]);
      Serial.print(") + -> Pin");
      Serial.print(cathode);
      Serial.print("(GPIO");
      Serial.print(displayPins[cathode]);
      Serial.println(") -");
      Serial.print("Record as: ");
      Serial.print(anode);
      Serial.print(",");
      Serial.print(cathode);
      Serial.println(",D#,SEG  (e.g., '2,5,D3,A' or '2,5,NONE')");
      Serial.println();
      Serial.println("Press button for next test...");
      
      // Wait for button press to continue
      waitForButtonPress();
      
      // Turn off
      digitalWrite(displayPins[anode], LOW);
    }
  }
  
  Serial.println();
  Serial.println("=== ALL TESTS COMPLETE ===");
  Serial.println("Please share your recordings in format:");
  Serial.println("  anode,cathode,digit,segment");
  Serial.println("Example:");
  Serial.println("  0,1,D1,A");
  Serial.println("  0,2,D1,B");
  Serial.println("  0,3,NONE");
  Serial.println("  etc...");
  Serial.println();
  Serial.println("Press button to restart tests...");
  
  waitForButtonPress();
  
  testNumber = 1;  // Reset counter for next run
}