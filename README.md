# AirPuter

## Install the release binary

1. Download **AirPuter.bin** from the [latest release](https://github.com/lukasdurjan/AirPuter-For-Cardputer-ADV-CAP-LoRa-1262-/releases/latest), or from [release/AirPuter.bin](release/AirPuter.bin) in this repository (use **Download raw file**). Every push to `main` automatically publishes a new versioned release.
2. Install [Python](https://www.python.org/downloads/) and esptool:

   ```bash
   python -m pip install --upgrade esptool
   ```

3. Connect the Cardputer by USB using a data cable. Close any serial monitor. If it does not connect, hold **G0** while powering on/resetting the device to enter download mode.
4. Flash the downloaded file. Replace `COM3` with your Cardputer's serial port (for example `/dev/ttyACM0` on Linux or `/dev/cu.usbmodem...` on macOS):

   ```bash
   python -m esptool --chip esp32s3 --port COM3 --baud 460800 write-flash 0x0 AirPuter.bin
   ```

5. Restart the Cardputer, then select your Wi-Fi network and enter its password on the device.

**AirPuter.bin is a complete image** containing the bootloader, partition table, OTA initialization data, and application. Always flash it at **0x0**. Installation replaces the current firmware. The raw PlatformIO `firmware.bin` is only the application and cannot be installed using this command.

See the [official esptool flashing documentation](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/flashing-firmware.html) for connection troubleshooting.

**Live aircraft tracker for the M5Stack Cardputer.**

AirPuter shows nearby aircraft directly on the Cardputer display using live ADS-B data. It includes an interactive OpenStreetMap view, a lightweight outline map, a sortable flight list, aircraft details, and GPS support through the Cardputer LoRa/GPS CAP.

## Features

- Live nearby-aircraft tracking using the ADSB.lol API
- OpenStreetMap raster map with aircraft position and track
- Lightweight outline-map mode with country outlines and city labels
- Map pan and 5 / 10 / 25 / 50 / 100 / 200 NM zoom levels
- Nearby-flight list sorted by distance from the observer
- Global SQUAWK7500 list available from the Tab screen cycle
- Persistent display Auto Dim setting: 30, 60, 120 seconds or Off
- Detailed aircraft view: callsign, ICAO, registration, type, route, distance/bearing, altitude, groundspeed, track, vertical speed, squawk, coordinates, emergency state and data age when available
- Hold `Alt` on an active flight detail to temporarily show its aircraft photo; release it to return to the data
- Route lookup through ADSB.lol VRS standing data
- GPS input through the LoRa/GPS CAP (UART RX on GPIO 15, 115200 baud)
- Automatic Poprad, Slovakia fallback position when no valid GPS fix is available
- Wi-Fi network scanner and on-device password entry
- Saved Wi-Fi credentials after a successful connection
- Map mode preference retained across restarts
- Network work runs separately so GPS and UI remain responsive

## Hardware

- M5Stack Cardputer / compatible StampS3-based Cardputer target
- Wi-Fi connection for live aircraft data and map tiles
- Optional LoRa/GPS CAP for live observer position

The current PlatformIO target is `m5stack-stamps3` and uses the `M5Cardputer` library.

## Controls

| Key | Action |
| --- | --- |
| Arrow keys | Pan map; move through lists/menus |
| `I` / `O` | Zoom in / out |
| `Opt` | Open settings / return from settings |
| `Tab` | Cycle map, flight list and global SQUAWK7500 list; details → map |
| `Enter` | Open selected flight / activate menu item |
| Hold `Alt` | Show the aircraft photo while viewing active flight details |
| `Esc` | Details → list; Wi-Fi screen → previous menu; on map restore GPS following |
| `R` | Rescan Wi-Fi networks while in Wi-Fi list |

Aircraft photos and photographer credits are provided by the PlaneSpotters.net public photo API and require Wi-Fi.
| `Backspace` | Delete character during Wi-Fi password entry |

## Map modes

### Tiles

Downloads visible PNG tiles from OpenStreetMap while the tile map is displayed. AirPuter displays the required OpenStreetMap attribution on screen.

### Outline

A compact built-in black map with country outlines and city labels. It does not download raster map tiles and is useful when you want a cleaner, lighter display.

## GPS

AirPuter listens for NMEA GPS data at **115200 baud** on **GPIO 15**. A recent valid GPS fix becomes the observer position and the map follows it by default.

Without a valid GPS fix, AirPuter falls back to:

```text
49.0550 N, 20.3010 E
```

This is the default Poprad-area position and can be changed in `src/app.cpp`.

## Building with PlatformIO

1. Install Visual Studio Code and PlatformIO.
2. Clone this repository.
3. Open the project folder in VS Code.
4. Build the `m5stack-cardputer` environment.
5. Connect the Cardputer and upload from PlatformIO.

Command-line build:

```bash
pio run -e m5stack-cardputer
```

Upload:

```bash
pio run -e m5stack-cardputer -t upload
```

Serial monitor:

```bash
pio device monitor -b 115200
```

## Wi-Fi configuration

AirPuter can scan for Wi-Fi networks and enter credentials directly on the Cardputer. Successfully connected credentials are stored for subsequent starts.

`src/main.cpp` contains optional fallback credentials. **Do not commit personal Wi-Fi credentials to a public repository.** Leave them empty for public builds:

```cpp
const char* WIFI_SSID = "";
const char* WIFI_PASS = "";
```

## Data and map sources

Aircraft data and route information are obtained from **ADSB.lol**. Raster map tiles are provided by **OpenStreetMap**.

Map data © OpenStreetMap contributors. See `MAP_SOURCES.md` for map-source and attribution details.

## Release binary

Every `pio run -e m5stack-cardputer` build also generates **release/AirPuter.bin**. This folder is tracked by Git, so you can push the ready-to-install binary together with the source:

```bash
pio run -e m5stack-cardputer
git add release/AirPuter.bin
git commit -m "Update release firmware"
git push origin main
```

Commit any source changes together with the binary so they match. After every push to `main`, GitHub Actions builds the firmware, finds the highest existing `vMAJOR.MINOR.PATCH` tag, increases its patch number, and publishes a versioned GitHub Release with `AirPuter.bin`. For example, `v1.0.1` becomes `v1.0.2`. With no version tags, the first release is `v1.0.0`. The release tag points to the commit that was built. No manual tagging is needed.

To choose a specific version instead (for example a new major or minor version), push a new version tag:

```bash
git tag v1.1.0
git push origin v1.1.0
```

Use a new version number for each release. GitHub Actions attaches the complete `AirPuter.bin` image to the release; installation instructions are at the top of this README.

## Support

AirPuter splash screen and project support:

**Ko-fi:** `https://ko-fi.com/lukaslukee`
