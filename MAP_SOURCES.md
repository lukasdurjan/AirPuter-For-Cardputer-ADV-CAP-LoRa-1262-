The map downloads visible PNG tiles from [OpenStreetMap](https://www.openstreetmap.org/)
after the current position is known. A valid LoraCap GPS fix defines the center;
otherwise it uses Poprad. The tile map uses downloaded raster tiles; the
optional embedded outline layer is described below.

Press **I** to zoom in and **O** to zoom out. Radius steps are 5, 10, 25, 50,
100 and 200 nautical miles. Aircraft positions and map tiles use the same Web
Mercator projection and scale, including longitude wrapping at the dateline.

Tiles are requested over HTTPS from `https://tile.openstreetmap.org/{z}/{x}/{y}.png`
using the application's User-Agent. `MAP_TILE_SERVER` in `src/main.cpp` can be
changed to a compatible server. Only tiles intersecting the visible viewport
are downloaded; no areas or other zoom levels are prefetched.

Received tiles are cached in LittleFS on the device's existing filesystem
partition, reserved by this application for the map cache. On first use the
empty partition is initialized automatically. Normal firmware upload does not
require a separate filesystem upload. The cache retains up to 36 tiles, subject
to available space, and keeps HTTP expiry, ETag and Last-Modified metadata.
Expired entries are validated conditionally; revisiting cached areas does not
redownload fresh tiles. Time is synchronized using NTP for persistent expiry.

GPS, keyboard handling and drawing continue while the network worker requests
map and aircraft data. Successfully received tiles are drawn as they arrive;
missing tiles use a plain background. Cached tiles remain usable on WiFi or
server failure. The map background is decoded again only after a visible tile,
zoom or position change.

Map data © OpenStreetMap contributors, licensed under the
[Open Database License](https://www.openstreetmap.org/copyright).
Visible attribution is included in the display footer.
Tile requests follow the [OSMF Tile Usage Policy](https://operations.osmfoundation.org/policies/tiles/).

The optional **Outline** setting uses a compact worldwide vector layer of
country borders, coastlines and cities on a black background. Raster tile
downloads are disabled in this mode. Borders (1:10m) and coastlines (1:50m)
come from [Natural Earth](https://www.naturalearthdata.com/) (public domain);
city names and locations come from [GeoNames cities15000](https://download.geonames.org/export/dump/)
under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
Names are converted to ASCII and geometry simplified for the small display.
Regenerate this layer with `python tools/generate_outline.py`.
See `CONTROLS.md` for navigation and settings.
