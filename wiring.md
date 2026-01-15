# Marine Battery Monitor - Wiring Guide

## Components Needed

1. ESP32 Development Board
2. INA226 Current Sensor Module
3. 0.0015Ω (1.5 milliohm) Shunt Resistor
4. 2x Common Anode 3-Digit 7-Segment LED Displays
5. 8x 330Ω Resistors (for segment current limiting)
6. Breadboard or PCB
7. Jumper wires

## Display Wiring

### Common Anode 7-Segment Display Pinout
```
Standard 3-digit common anode display typically has:
- 3 common anode pins (one per digit)
- 8 segment pins (A, B, C, D, E, F, G, DP) shared across digits
```

### Segment Connections (with 330Ω resistors)
All segment pins are shared between both displays:

```
ESP32 Pin → 330Ω Resistor → Display Segment
GPIO23 → [330Ω] → Segment A (all 6 digits)
GPIO19 → [330Ω] → Segment B (all 6 digits)
GPIO18 → [330Ω] → Segment C (all 6 digits)
GPIO5  → [330Ω] → Segment D (all 6 digits)
GPIO17 → [330Ω] → Segment E (all 6 digits)
GPIO16 → [330Ω] → Segment F (all 6 digits)
GPIO4  → [330Ω] → Segment G (all 6 digits)
GPIO2  → [330Ω] → Segment DP (all 6 digits)
```

### Digit Select Connections (Common Anodes)
Each digit's common anode connects directly to ESP32:

**Voltage Display (leftmost display):**
```
GPIO15 → Voltage Digit 1 Common Anode (tens)
GPIO13 → Voltage Digit 2 Common Anode (ones + decimal)
GPIO12 → Voltage Digit 3 Common Anode (tenths)
```

**Current Display (rightmost display):**
```
GPIO14 → Current Digit 1 Common Anode (sign/tens)
GPIO27 → Current Digit 2 Common Anode (ones)
GPIO26 → Current Digit 3 Common Anode (tenths or ones)
```

## INA226 Current Sensor Wiring

### INA226 Module to ESP32
```
INA226 VCC → ESP32 3.3V
INA226 GND → ESP32 GND
INA226 SDA → GPIO21
INA226 SCL → GPIO22
```

### INA226 to Shunt and Battery
```
Battery (+) → Shunt (0.0015Ω) → Load (+)
                ↓
           INA226 IN+ and IN-
           measure voltage across shunt
```

**Important:** The shunt must be in series with the battery negative or positive rail.

## Power Considerations

- **ESP32 Operating Voltage:** 3.3V (board usually has onboard regulator for 5V USB input)
- **LED Current per Segment:** ~20mA (with 330Ω resistors @ 3.3V)
- **Total Display Current:** ~40-50mA (only 1/6 digits lit at a time due to multiplexing)
- **Total System Current:** ~200-300mA typical

## Testing Procedure

1. **Power on ESP32** - Should see serial output
2. **Check WiFi AP** - "Fidelio" network should appear
3. **Check displays** - Should show voltage and current
4. **Connect to web interface** - http://192.168.4.1
5. **Verify readings** - Compare display vs. web interface values

## Troubleshooting

**Displays not lighting:**
- Check common anode connections (should go HIGH when digit selected)
- Verify segment resistors (330Ω)
- Check for common anode vs common cathode displays

**Flickering displays:**
- Normal at very low brightness
- May indicate insufficient power supply

**Wrong segments lighting:**
- Check segment pin connections
- Verify A-G pin mapping to your specific display

**INA226 not responding:**
- Check I2C address (default 0x40)
- Verify SDA/SCL connections
- Check pull-up resistors on I2C lines (often built into modules)

## Pin Summary

| Function | ESP32 GPIO | Notes |
|----------|-----------|-------|
| I2C SDA | 21 | INA226 |
| I2C SCL | 22 | INA226 |
| Segment A | 23 | All displays + 330Ω |
| Segment B | 19 | All displays + 330Ω |
| Segment C | 18 | All displays + 330Ω |
| Segment D | 5 | All displays + 330Ω |
| Segment E | 17 | All displays + 330Ω |
| Segment F | 16 | All displays + 330Ω |
| Segment G | 4 | All displays + 330Ω |
| Segment DP | 2 | All displays + 330Ω |
| Voltage Digit 1 | 15 | Tens |
| Voltage Digit 2 | 13 | Ones |
| Voltage Digit 3 | 12 | Tenths |
| Current Digit 1 | 14 | Sign/Tens |
| Current Digit 2 | 27 | Ones |
| Current Digit 3 | 26 | Tenths |

Total pins used: 16 (excluding power/ground)