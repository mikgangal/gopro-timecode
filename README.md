# GoPro Time Sync

Automatic time synchronization for GoPro Hero 9/10/11 cameras using ESP32 and DS3231 RTC module.

## Overview

This project automatically synchronizes your GoPro camera's internal clock with an accurate DS3231 RTC module via an ESP32. The ESP32 connects to the GoPro over BLE, enables its WiFi access point, and sets the time using the legacy HTTP API. It continuously monitors the connection and automatically re-syncs when the GoPro is powered back on.

## Features

✅ **Automatic Time Sync** - Reads accurate time from DS3231 RTC and syncs to GoPro  
✅ **Auto-Reconnection** - Detects GoPro power cycles and automatically reconnects  
✅ **Periodic Updates** - Re-syncs time every hour while connected  
✅ **Full BLE + WiFi Handling** - Manages BLE connection, WiFi AP enable, and WiFi connection  
✅ **Clean Code** - Simplified, well-documented, production-ready

## Hardware Requirements

- **ESP32 Development Board** (any variant with BLE support)
- **DS3231 RTC Module** (I2C interface with battery backup)
- **GoPro Hero 9, 10, or 11 Camera**
- USB cable for programming ESP32
- Jumper wires for connections

## Wiring

Connect the DS3231 RTC module to the ESP32:

| DS3231 | ESP32 |
|--------|-------|
| VCC    | 3.3V  |
| GND    | GND   |
| SDA    | GPIO 21 |
| SCL    | GPIO 22 |

> **Note:** Ensure your DS3231 has a backup battery (CR2032) installed to maintain accurate time when powered off.

## Software Requirements

- [PlatformIO](https://platformio.org/) (recommended) or Arduino IDE
- Libraries (auto-installed with PlatformIO):
  - `NimBLE-Arduino` @ ^1.4.2
  - `RTClib` @ ^2.1.1
  - ESP32 Arduino Framework

## Installation

### Using PlatformIO (Recommended)

1. Clone this repository:
   ```bash
   git clone https://github.com/yourusername/gopro-timecode.git
   cd gopro-timecode
   ```

2. Open the project in PlatformIO:
   ```bash
   cd "gopro time sync"
   pio run
   ```

3. Upload to your ESP32:
   ```bash
   pio run --target upload
   ```

4. Monitor the serial output:
   ```bash
   pio device monitor
   ```

### Using Arduino IDE

1. Install the required libraries via Library Manager:
   - NimBLE-Arduino
   - RTClib by Adafruit

2. Open `gopro time sync/src/main.cpp` in Arduino IDE

3. Select your ESP32 board and upload

## How It Works

### Initial Setup Flow

1. **RTC Initialization** - Reads current time from DS3231 module
2. **BLE Scan** - Scans for nearby GoPro cameras
3. **BLE Connection** - Connects to the GoPro via Bluetooth LE
4. **WiFi Credentials** - Reads SSID and password from GoPro
5. **Enable WiFi AP** - Tells GoPro to turn on its WiFi access point
6. **WiFi Connection** - ESP32 connects to GoPro's WiFi network
7. **Time Sync** - Sets GoPro time via HTTP API
8. **Monitoring** - Continuously monitors connection status

### Automatic Reconnection

When the GoPro is powered off and back on:

1. ESP32 detects WiFi disconnection
2. Waits 30 seconds for GoPro to boot
3. Performs full reconnection routine (BLE → WiFi AP → WiFi)
4. Immediately syncs time after reconnection
5. Continues monitoring

### Periodic Sync

While connected, the ESP32 automatically re-syncs the time every hour to maintain accuracy.

## API Endpoint Used

The project uses the **legacy GoPro HTTP API** endpoint:

```
GET http://10.5.5.9/gp/gpControl/command/setup/date_time?p=%YY%MM%DD%HH%MM%SS
```

Where parameters are hex-encoded:
- `YY` - Year (2 digits, e.g., 0x19 = 2025)
- `MM` - Month (1-12)
- `DD` - Day (1-31)
- `HH` - Hour (0-23)
- `MM` - Minute (0-59)
- `SS` - Second (0-59)

## Configuration

You can adjust timing parameters in `main.cpp`:

```cpp
#define SCAN_TIME_SECONDS 10              // BLE scan duration
#define BLE_CONNECT_TIMEOUT_MS 15000      // BLE connection timeout
#define WIFI_CONNECT_TIMEOUT_MS 20000     // WiFi connection timeout
#define AP_READY_POLL_ATTEMPTS 25         // WiFi AP ready polling attempts
```

Reconnection timing (in `loop()`):
```cpp
if (!isConnected && (millis() - lastReconnectAttempt > 30000)) {  // 30 seconds
    // Reconnection logic
}

if (isConnected && (millis() - lastSync > 3600000)) {  // 1 hour
    // Periodic sync logic
}
```

## Python Script (Windows Only)

The `scripts/wifi_ap_enable.py` script is a **standalone Windows demonstration** that shows how to connect to a GoPro using Python's `open_gopro` library. 

**This script is NOT required for the ESP32 project** and serves only as a reference implementation for understanding the GoPro BLE/WiFi connection process.

To run the Python script (optional):

```bash
pip install -r requirements.txt
python scripts/wifi_ap_enable.py
```

> **Note:** The Python script requires Windows and a Bluetooth adapter. It was used during development to understand the GoPro API but is not part of the final ESP32 solution.

## Serial Monitor Output Example

```
==================================
ESP32 GoPro Time Sync
==================================

[RTC] Initializing DS3231 RTC...
[RTC] Current time: 2025-11-16 04:38:04
[BLE] Initializing BLE...
[BLE] Scanning for GoPro devices...
[BLE] Found GoPro: GoPro 9953 (f4:03:28:96:36:4a)
[BLE] Connecting to GoPro at f4:03:28:96:36:4a...
[BLE] Connected to GoPro
[BLE] Getting WiFi SSID...
[BLE] WiFi SSID: GP50029953
[BLE] Getting WiFi password...
[BLE] WiFi password: r#P-jP7-bD3
[BLE] Enabling WiFi AP...
[BLE] WiFi AP enable command sent successfully
[BLE] AP Mode status: 0x03
[BLE] AP is ready and broadcasting!
[WiFi] Connecting to GoPro AP: GP50029953...
[WiFi] Connected! IP: 10.5.5.110
[HTTP] Setting GoPro date/time...
[HTTP] URL: http://10.5.5.9/gp/gpControl/command/setup/date_time?p=%19%0b%10%04%26%04
[HTTP] Time synchronized successfully!

[SUCCESS] Date/time synchronized!

==================================
Setup complete!
==================================
```

## Troubleshooting

### GoPro Not Found
- Ensure GoPro Bluetooth is enabled
- Put GoPro in pairing mode: `Preferences` > `Connections` > `Connect Device` > `GoPro App`
- Check if GoPro is already connected to another device

### BLE Connection Fails
- Move ESP32 closer to GoPro
- Restart both ESP32 and GoPro
- Check serial monitor for specific error messages

### WiFi Connection Fails
- Verify WiFi credentials were read correctly
- Ensure GoPro WiFi AP is enabled (check GoPro screen)
- Check if another device is connected to GoPro WiFi

### Time Not Syncing
- Verify DS3231 RTC has correct time
- Check if DS3231 battery is installed
- Monitor serial output for HTTP response codes
- Ensure GoPro firmware is up to date

### Reconnection Issues
- Wait at least 30 seconds after powering GoPro back on
- Check serial monitor for reconnection attempts
- Verify GoPro remains in WiFi mode after boot

## Compatible GoPro Models

This project has been tested with:
- ✅ GoPro Hero 9 Black
- ✅ GoPro Hero 10 Black (should work)
- ✅ GoPro Hero 11 Black (should work)

Older models (Hero 5-8) may work but use different BLE protocols and are not officially supported.

## Project Structure

```
gopro-timecode/
├── gopro time sync/          # ESP32 PlatformIO project
│   ├── src/
│   │   └── main.cpp          # Main ESP32 application
│   ├── platformio.ini        # PlatformIO configuration
│   └── lib/                  # Libraries folder
├── scripts/
│   └── wifi_ap_enable.py     # Python demo script (Windows only)
├── requirements.txt          # Python dependencies (for demo script)
└── README.md                 # This file
```

## Contributing

Contributions are welcome! Please feel free to submit issues or pull requests.

## License

This project is open source and available under the MIT License.

## Acknowledgments

- [Open GoPro API Documentation](https://gopro.github.io/OpenGoPro/)
- [NimBLE-Arduino Library](https://github.com/h2zero/NimBLE-Arduino)
- [RTClib by Adafruit](https://github.com/adafruit/RTClib)

## Author

Created for automatic GoPro timecode synchronization in multi-camera setups.

---

**Note:** This project is not affiliated with or endorsed by GoPro, Inc. GoPro is a registered trademark of GoPro, Inc.

