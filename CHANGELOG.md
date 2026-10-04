# Changelog

## 2026-10-04

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

