AirPuter controls

At startup, a short splash screen displays `Coffee flies the World` and
`https://ko-fi.com/lukaslukee` before the map starts.

| Key | Action |
| --- | --- |
| Arrows (`;`, `.`, `,`, `/` on the Cardputer) | Pan the map; holding repeats movement. In lists and menus, up/down move exactly one item. The printed arrow keys work with or without Fn. |
| I / O | Zoom the map in/out (5–200 NM). |
| Opt | Open settings, or close settings to the previous screen. |
| Tab | Cycle map → flight list → global SQUAWK7500 list → map; from details return to the map. |
| Enter | Open the selected flight or activate a menu/network item. |
| Hold Alt | Temporarily show the aircraft photo from an active flight detail; release to return. |
| Esc (top-left key, optionally with Fn) | Details → list; WiFi screens → previous menu. On the map, restore GPS following. Password entry requires Fn+Esc so a plain backtick can be typed. |
| R in the WiFi list | Scan for networks again. |
| Backspace in the password form | Delete a character. |

Settings include Tiles/Outline, WiFi networks, Center on GPS, persistent Auto Dim (30/60/120s/Off) and Back.
Outline is a black map with city labels and country outlines, with no raster
tile downloads. The setting survives restart. Open networks connect directly;
protected networks prompt for a masked password. Successfully connected WiFi
credentials are retained for the next startup, falling back to `src/main.cpp`
settings if no saved credentials exist.

The aircraft list is sorted by distance from the current LoraCap GPS position,
or Poprad when GPS has no valid fix. Panning only changes the map viewport.
The selected aircraft is tracked by its ICAO identifier across downloads, and
distances are recalculated as the device moves. When details are opened, the
first row loads the departure and arrival airport for the selected callsign.
Details also include callsign, ICAO, registration, type, distance/bearing, altitude, groundspeed, track,
vertical speed, squawk, coordinates, emergency and data age when available.
Route data comes from the ADSB.lol VRS standing-data service. Missing fields
display `--`. If the flight disappears from the latest response,
its open detail remains visible with a `Last position` label.

WiFi scanning, connection, map downloads and aircraft downloads run on the
network worker. GPS and the UI keep updating. Map tiles are only requested
while the tile map is visible.
