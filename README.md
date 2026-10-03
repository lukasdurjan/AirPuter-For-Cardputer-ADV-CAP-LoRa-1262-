# AirPuter

**Live aircraft tracker for the M5Stack Cardputer.**

AirPuter shows nearby aircraft directly on the Cardputer display using live ADS-B data. It includes an interactive OpenStreetMap view, a lightweight outline map, a sortable flight list, aircraft details, and GPS support through the Cardputer LoRa/GPS CAP.

## Features

- Live nearby-aircraft tracking using the ADSB.lol API
- OpenStreetMap raster map with aircraft position and track
- Lightweight outline-map mode with country outlines and city labels
- Map pan and 5 / 10 / 25 / 50 / 100 / 200 NM zoom levels
- Nearby-flight list sorted by distance from the observer
- Detailed aircraft view: callsign, ICAO, registration, type, route, distance/bearing, altitude, groundspeed, track, vertical speed, squawk, coordinates, emergency state and data age when available
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
| `Tab` | Switch map and flight list; details → map |
| `Enter` | Open selected flight / activate menu item |
| `Esc` | Details → list; Wi-Fi screen → previous menu; on map restore GPS following |
| `R` | Rescan Wi-Fi networks while in Wi-Fi list |
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

The release firmware is built for the `m5stack-cardputer` PlatformIO environment. Flashing requirements depend on the installer/flashing tool being used; keep the bootloader and partition layout consistent with the PlatformIO project.

## Support

AirPuter splash screen and project support:

**Ko-fi:** `https://ko-fi.com/lukaslukee`
