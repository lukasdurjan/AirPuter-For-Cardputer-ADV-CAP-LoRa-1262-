# AirPuter controls

At startup, the splash screen displays `Coffee flies the World` and starts GPS/NMEA processing in the background before the map appears.

| Key | Action |
| --- | --- |
| Arrows (`;`, `.`, `,`, `/`) | Pan in Free View; holding repeats movement. Panning is blocked during GPS Follow. Up/down navigate lists and menus. |
| `C` | Toggle GPS Follow and Free View. Follow requires a valid fix; disabling it retains the last displayed GPS position. |
| `I` / `O` | Zoom the map between 5 and 200 NM. |
| Tap `0`–`9` | Open the corresponding saved Favorite Place. An empty slot does nothing. |
| Hold `0`–`9` for 800 ms | Save the current map centre to the corresponding Favorite Place. A short beep confirms success. |
| `Opt` | Open settings, or close settings to the previous screen. |
| `Tab` | Cycle map → flight list → global SQUAWK7500 list → map; details → map. |
| `Enter` | Open a selected nearby flight; track a selected SQUAWK7500 aircraft; activate a menu or network item. |
| Hold `Alt` | Temporarily show the aircraft photo from an active flight detail; release to return. |
| `Esc` | Details → list; Wi-Fi screens → previous menu; on the map restore GPS Follow when a fix is available. Password entry requires Fn+Esc. |
| `R` in Wi-Fi list | Scan for networks again. |
| `Backspace` | Delete a character in password or Home-position entry. |

## Map navigation indicators

- A fixed white crosshair with a yellow centre always marks the map centre.
- `Follow` in cyan means the map follows GPS and panning is locked.
- `Free View` in grey means panning is enabled.
- Corner brackets around the observer show active GPS Follow: cyan for a quality fix and orange for a weaker valid fix.
- The GPS header dot pulses red while searching, is orange for a weaker valid fix, and green for a quality fix.

## Settings

Settings include Tiles/Outline, Wi-Fi networks, Center on GPS, Set home position, Favorite Places, persistent Auto Dim (30/60/120 seconds/Off), Refresh Interval (10/15/20/25/30 seconds), and Back.

`Set home position` accepts `latitude,longitude` and stores it in NVS. It replaces the Poprad fallback only after valid coordinates are saved. `Favorite Places` displays all ten persistent number-key slots.

The aircraft list follows the current viewport and sorts aircraft by distance from the observer. Panning and zooming re-filter cached aircraft without bypassing the configured ADS-B refresh timer. Moving aircraft are predicted between real responses for at most 60 seconds.

In the global SQUAWK7500 list, select an aircraft and press `Enter` to follow it on the map. Press `C` with a valid GPS fix or `Esc` to leave aircraft tracking and restore GPS Follow.

Wi-Fi scanning, connection, map downloads, aircraft requests, routes and photos run on the network worker. GPS and UI processing remain responsive. Photo download transport failures and incomplete transfers receive one automatic clean retry.
