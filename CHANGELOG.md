# Changelog

## v1.0.17 — 2026-10-14

- Added resource-light dead reckoning of aircraft positions between ADS-B refreshes, rendered at up to 4 FPS and capped at 60 seconds. Every successful response resets aircraft to real server positions.
- Added automatic clean retry for aircraft-photo `HTTP -1` connection failures and incomplete JPEG transfers, including JPEG end-marker validation.
- Started GPS parsing during the splash screen and added Serial Monitor diagnostics for satellites, HDOP, data age, fix acquisition and fix loss.
- Added colour-coded GPS quality, permanent map-centre crosshair, compact `Follow` / `Free View` state and coloured follow brackets.
- Changed `C` to toggle GPS Follow. Free View retains the last displayed GPS position, while panning is disabled during Follow.
- Added a persistent custom Home fallback while retaining Poprad when no Home is configured.
- Added ten persistent Favorite Places: tap `0`–`9` to open, hold a number for 800 ms to save the current map centre, and beep after a successful save.
- Added a Favorite Places overview to Settings and documented all updated controls.
- Improved Wi-Fi-not-configured guidance and retained the earlier reliable scan and map PNG/TLS heap fixes.
- Skipped unpublished versions `v1.0.15` and `v1.0.16`; automated release selection advances directly from `v1.0.14` to `v1.0.17`.

## 2026-10-04

- Documented server-side HTTP 429 rate limits, automatic recovery, and stale aircraft data during cooldown.

- Synchronized the flight list with the current map viewport; zoom/pan no longer bypass the ADS-B refresh timer.
- Expanded aircraft queries to cover map corners and replaced first-50 truncation with bounded stable sampling of the complete response.

- Added a persistent 1/2/5/10/30-second aircraft refresh setting (10 seconds by default) and exponential 60–300-second backoff after HTTP 429 responses.
- Temporarily removed the experimental ATC audio streaming feature and its settings to restore firmware stability.
- Fixed silent ATC playback by initializing the M5Cardputer speaker before starting the decoder task.
- Added HTTPS and redirect support for custom ATC streams such as dynamic `d.liveatc.net` MP3 endpoints.
- Added persistent custom ATC MP3/Icecast playback, Settings controls for ATC On/Off and source URL, plus W/A volume control. No third-party streams are bundled.
- Added selectable SQUAWK7500 aircraft tracking that keeps the map centred on the chosen aircraft until Esc returns to GPS.
- Fixed the OSM map disappearing when the first GPS fix moves the view; the previous map remains visible until new GPS-area tiles are ready.
- Added push-triggered GitHub Actions releases with automatic patch versioning, repository BIN updates and Latest Release publishing.
- Fixed photo decoding by converting unsupported progressive thumbnails to baseline JPEG for M5GFX.
- Fixed aircraft photo HTTP 403 errors and chunked JSON parsing failures; photo downloads now start on the first Alt press.
- Added an aircraft photo view while holding Alt on an active flight detail.
- Added a global SQUAWK7500 screen to the Tab navigation cycle.
- Aircraft queries now follow the panned map center at every zoom level.
- Added persistent Auto Dim settings for 30, 60, 120 seconds and Off.

