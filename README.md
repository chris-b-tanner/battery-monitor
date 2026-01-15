# Marine Battery Monitor - INA226 Data Logger

A yacht/boat battery monitoring system with web interface for tracking voltage and current over time.

## Features
- 📊 Logs data every 10 minutes
- 💾 Stores up to 3 days of data (432 points)
- 🔄 Data persists through power outages
- 📱 Mobile-responsive web interface
- 📈 Real-time voltage and current charts
- 🔋 State of Charge (SOC) tracking with amp-hour integration
- 📐 Peukert's Law applied for accurate SOC at varying discharge rates
- ⚡ Automatic full battery detection (resets SOC to 100%)
- 🌐 Self-contained (no internet required)
- ⚡ Tracks both discharge (night) and charging (day/solar)

## Marine Battery Use Case
This system is designed for marine battery monitoring where:
- **Night/Evening**: Battery discharge (negative current) from house loads
- **Day**: Battery charging (positive current) from solar panels
- **Voltage tracking**: Monitor battery state of charge
- **Current tracking**: See consumption patterns and solar charging efficiency
- **SOC tracking**: Real-time state of charge percentage based on amp-hour integration

### How SOC Tracking Works
1. **Initialization**: Starts at 100% or loads saved value from flash
2. **Integration**: Every 10 seconds, calculates amp-hours consumed/charged
3. **Peukert Correction**: When discharging, applies Peukert's Law to account for reduced capacity at higher discharge rates
4. **Calculation**: `Remaining Ah = Previous Ah + (Current × Time × Peukert Factor)`
5. **Percentage**: `SOC% = (Remaining Ah / 300 Ah) × 100`
6. **Full Detection**: Automatically resets to 100% when battery reaches full charge
7. **Persistence**: SOC saved every minute and restored after power loss

## Setup Instructions

### 1. Upload the Filesystem (IMPORTANT!)
Before uploading the main code, you must upload the web interface files to the ESP32's filesystem:

**In PlatformIO:**
- Click on the PlatformIO icon in the sidebar
- Under "PROJECT TASKS" → "Platform" → click "Build Filesystem Image"
- Then click "Upload Filesystem Image"

**Or via command line:**
```bash
pio run --target buildfs
pio run --target uploadfs
```

This uploads the `data/index.html` file to the ESP32's LittleFS filesystem.

### 2. Upload the Code
After uploading the filesystem, upload the main code as normal:
- Click "Upload" button in PlatformIO, or
- Run `pio run --target upload`

### 3. Connect and View
1. Wait for the ESP32 to boot
2. Connect to WiFi network "Fidelio" (no password)
3. Open browser and navigate to `http://192.168.4.1`

## Configuration

### Test Mode
Test mode is **enabled by default** and provides 24 hours of realistic marine battery data at 10-minute intervals, showing:
- Night discharge patterns (negative current)
- Solar charging during the day (positive current)
- Realistic voltage fluctuations (11-14V range)

To disable test mode and use real sensor data:
- Edit `src/main.cpp`
- Change `#define IS_TEST true` to `#define IS_TEST false`
- Upload code

### Shunt Resistor
Current shunt resistor value: **0.0015Ω** (1.5 milliohm)
Maximum measurable current: **~50A**

To change these values, edit in `src/main.cpp`:
```cpp
#define SHUNT_RESISTOR 0.0015
ina.setMaxCurrentShunt(50.0, SHUNT_RESISTOR);
```

### Battery Capacity
Battery bank capacity: **300Ah** (configured for lead acid)

To change capacity, edit in `src/main.cpp`:
```cpp
#define BATTERY_CAPACITY_AH 300.0
#define PEUKERT_EXPONENT 1.1       // Lead acid: 1.05-1.4, AGM: 1.05-1.15, Lithium: ~1.0
#define C20_RATE 15.0              // Capacity / 20h (300Ah / 20h = 15A)
```

**Peukert's Law:**
The system applies Peukert's Law to account for reduced effective capacity at higher discharge rates. When discharging, the effective amp-hours consumed are multiplied by `(I/C20)^(n-1)` where:
- I = discharge current (A)
- C20 = 20-hour discharge rate (15A for 300Ah battery)
- n = Peukert exponent (1.1 for typical lead acid)

This means discharging at 30A consumes more "effective" amp-hours than the actual current would suggest.

**Full Battery Detection:**
Battery is considered full when:
- Voltage ≥ 13.8V AND
- Current < 1.0A (charging/discharging)
- Conditions sustained for 60 seconds

When detected, SOC resets to 100%. Adjust thresholds in `src/main.cpp`:
```cpp
#define FULL_VOLTAGE_THRESHOLD 13.8
#define FULL_CURRENT_THRESHOLD 1.0
#define FULL_DETECTION_TIME 60000
```

## File Structure
```
ina226_test/
├── platformio.ini          # PlatformIO configuration
├── src/
│   └── main.cpp           # Main ESP32 code
└── data/
    └── index.html         # Web interface (uploaded to ESP32)
```

## Troubleshooting

**Charts not loading:**
- Make sure you uploaded the filesystem image before uploading code
- Check Serial Monitor for "LittleFS mounted successfully"

**No WiFi network:**
- Network name: "Fidelio"
- No password required
- Default IP: 192.168.4.1

**INA226 not found:**
- Check I2C wiring: SDA=GPIO21, SCL=GPIO22
- Verify I2C address (default: 0x40)
- Check power to INA226 module