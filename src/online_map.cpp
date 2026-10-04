#include <Arduino.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <time.h>
#include "online_map.h"

extern const char* MAP_TILE_SERVER;

namespace OnlineMap {
constexpr uint32_t CACHE_MAGIC = 0x41504D33;
constexpr uint32_t DEFAULT_TTL = 7 * 24 * 60 * 60;
constexpr size_t MAX_TILE_BYTES = 80000;
constexpr int MAX_CACHE_TILES = 36;
constexpr char USER_AGENT[] = "AirPuter/1.3";

struct Metadata {
    uint32_t magic = CACHE_MAGIC;
    uint32_t expires = 0;
    uint32_t fetched = 0;
    uint32_t size = 0;
    uint32_t ttl = DEFAULT_TTL;
    char etag[96] = {};
    char modified[64] = {};
};

struct TileKey {
    int zoom = -1, x = 0, y = 0;
    TileKey() = default;
    TileKey(int z, int column, int row) : zoom(z), x(column), y(row) {}
    bool equals(const TileKey& b) const { return zoom == b.zoom && x == b.x && y == b.y; }
};

struct Failure { TileKey key; uint32_t retryAt = 0; bool active = false; };
Failure failures[6];
portMUX_TYPE requestMux = portMUX_INITIALIZER_UNLOCKED;
double requestedLat = 49.055, requestedLon = 20.301;
int requestedRadius = 50;
bool enabled = true;
uint32_t tileVersion = 0;
SemaphoreHandle_t fsMutex = nullptr;
bool cacheReady = false;
M5Canvas background;
bool backgroundReady = false;
uint32_t drawnVersion = UINT32_MAX;
double drawnLat = 0, drawnLon = 0;
int drawnRadius = 0, drawnZoom = -1;
int visibleTiles = 0;
bool retryDecode = false;
char drawnStatus[24] = "Map waiting for WiFi";

String tilePath(const TileKey& key, const char* extension)
{
    return "/tiles/" + String(key.zoom) + "_" + String(key.x) + "_" +
           String(key.y) + extension;
}

Metadata readMetadata(const TileKey& key)
{
    Metadata metadata;
    metadata.magic = 0;
    File file = LittleFS.open(tilePath(key, ".meta"), "r");
    if (file)
    {
        if (file.read(reinterpret_cast<uint8_t*>(&metadata), sizeof(metadata)) != sizeof(metadata))
            metadata.magic = 0;
        file.close();
    }
    return metadata;
}

bool validCache(const TileKey& key, const Metadata& metadata)
{
    if (metadata.magic != CACHE_MAGIC || metadata.size < 24 || metadata.size > MAX_TILE_BYTES)
        return false;
    File file = LittleFS.open(tilePath(key, ".png"), "r");
    bool valid = file && file.size() == metadata.size;
    file.close();
    return valid;
}

bool fresh(const Metadata& metadata)
{
    time_t now = time(nullptr);
    // Without a valid clock, reuse the persistent cache instead of redownloading it.
    return now < 1600000000 || metadata.expires == UINT32_MAX || uint32_t(now) < metadata.expires;
}

uint32_t cacheTtl(const String& cacheControl)
{
    uint32_t ttl = DEFAULT_TTL;
    int start = cacheControl.indexOf("max-age=");
    if (start >= 0)
    {
        String value = cacheControl.substring(start + 8);
        long seconds = value.toInt();
        if (seconds > 0)
            ttl = uint32_t(seconds);
    }
    return ttl;
}

uint32_t expiry(uint32_t ttl)
{
    time_t now = time(nullptr);
    return now < 1600000000 ? UINT32_MAX : uint32_t(now) + ttl;
}

void publishTileChange()
{
    portENTER_CRITICAL(&requestMux);
    ++tileVersion;
    portEXIT_CRITICAL(&requestMux);
}

void begin()
{
    fsMutex = xSemaphoreCreateMutex();
    background.setColorDepth(8); // RGB332 keeps enough RAM for TLS and PNG decoding.
    backgroundReady = background.createSprite(240, 109) != nullptr;
    // This application's existing filesystem partition is reserved for map cache.
    cacheReady = fsMutex && LittleFS.begin(true);
    if (cacheReady)
        LittleFS.mkdir("/tiles");
    else
        strlcpy(drawnStatus, "Map cache unavailable", sizeof(drawnStatus));
}

MapProjection::View currentView()
{
    portENTER_CRITICAL(&requestMux);
    double lat = requestedLat, lon = requestedLon;
    int radius = requestedRadius;
    portEXIT_CRITICAL(&requestMux);
    return MapProjection::View(lat, lon, radius);
}

bool request(double latitude, double longitude, int radiusNM)
{
    auto old = currentView();
    MapProjection::View next(latitude, longitude, radiusNM);
    bool changed = old.zoom != next.zoom || old.firstX != next.firstX ||
                   old.lastX != next.lastX || old.firstY != next.firstY || old.lastY != next.lastY;
    portENTER_CRITICAL(&requestMux);
    requestedLat = latitude;
    requestedLon = longitude;
    requestedRadius = radiusNM;
    portEXIT_CRITICAL(&requestMux);
    return changed;
}

bool needed(const TileKey& key, const MapProjection::View& view)
{
    if (key.zoom != view.zoom || key.y < view.firstY || key.y > view.lastY)
        return false;
    for (int col = view.firstX; col <= view.lastX; ++col)
        if (view.tileX(col) == key.x) return true;
    return false;
}

bool reserveCache(size_t bytes, const MapProjection::View& view)
{
    // Evict older non-visible cache entries when the bounded cache becomes full.
    for (;;)
    {
        int count = 0;
        String oldestPath;
        uint32_t oldest = UINT32_MAX;
        File directory = LittleFS.open("/tiles");
        File entry = directory.openNextFile();
        while (entry)
        {
            String path = entry.path();
            if (path.endsWith(".meta"))
            {
                ++count;
                TileKey key;
                if (sscanf(path.c_str(), "/tiles/%d_%d_%d.meta", &key.zoom, &key.x, &key.y) == 3 &&
                    !needed(key, view))
                {
                    Metadata metadata;
                    if (entry.read(reinterpret_cast<uint8_t*>(&metadata), sizeof(metadata)) == sizeof(metadata) &&
                        metadata.fetched <= oldest)
                    {
                        oldest = metadata.fetched;
                        oldestPath = path;
                    }
                }
            }
            entry.close();
            entry = directory.openNextFile();
        }
        directory.close();
        if (count < MAX_CACHE_TILES && LittleFS.totalBytes() - LittleFS.usedBytes() >= bytes + 8192)
            return true;
        if (oldestPath.isEmpty())
            return false;
        String pngPath = oldestPath.substring(0, oldestPath.length() - 5) + ".png";
        LittleFS.remove(pngPath);
        LittleFS.remove(oldestPath);
    }
}

bool validatePng(const String& path, size_t size)
{
    static const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    uint8_t header[24] = {};
    File file = LittleFS.open(path, "r");
    bool valid = file && file.size() == size && file.read(header, sizeof(header)) == sizeof(header) &&
                 memcmp(header, signature, sizeof(signature)) == 0 &&
                 header[16] == 0 && header[17] == 0 && header[18] == 1 && header[19] == 0 &&
                 header[20] == 0 && header[21] == 0 && header[22] == 1 && header[23] == 0;
    file.close();
    return valid;
}

bool downloadTile(const TileKey& key, const Metadata& cached, bool haveCache)
{
    HTTPClient http;
    http.setUserAgent(USER_AGENT);
    http.useHTTP10(true);
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    String url = String(MAP_TILE_SERVER) + "/" + String(key.zoom) + "/" +
                 String(key.x) + "/" + String(key.y) + ".png";
    if (!http.begin(url)) return false;
    const char* headers[] = {"Content-Type", "Cache-Control", "ETag", "Last-Modified"};
    http.collectHeaders(headers, 4);
    if (haveCache)
    {
        if (cached.etag[0]) http.addHeader("If-None-Match", cached.etag);
        if (cached.modified[0]) http.addHeader("If-Modified-Since", cached.modified);
    }
    int code = http.GET();
    Serial.printf("Map tile %d/%d/%d HTTP %d\n", key.zoom, key.x, key.y, code);
    if (code == HTTP_CODE_NOT_MODIFIED && haveCache)
    {
        Metadata metadata = cached;
        metadata.ttl = cacheTtl(http.header("Cache-Control"));
        metadata.expires = expiry(metadata.ttl);
        xSemaphoreTake(fsMutex, portMAX_DELAY);
        File meta = LittleFS.open(tilePath(key, ".meta"), "w");
        bool saved = meta && meta.write(reinterpret_cast<const uint8_t*>(&metadata), sizeof(metadata)) == sizeof(metadata);
        meta.close();
        xSemaphoreGive(fsMutex);
        http.end();
        return saved;
    }
    int length = http.getSize();
    if (code != HTTP_CODE_OK || length < 24 || length > int(MAX_TILE_BYTES) ||
        !http.header("Content-Type").startsWith("image/png"))
    {
        http.end();
        return false;
    }
    auto view = currentView();
    // Abandon a response for an old zoom/position; never fetch outside the current view.
    if (!needed(key, view)) { http.end(); return true; }
    xSemaphoreTake(fsMutex, portMAX_DELAY);
    bool room = reserveCache(length, view);
    xSemaphoreGive(fsMutex);
    if (!room) { http.end(); return false; }

    String temporary = tilePath(key, ".tmp");
    File file = LittleFS.open(temporary, "w");
    int written = file ? http.writeToStream(&file) : -1;
    file.close();
    Metadata metadata;
    metadata.size = length;
    metadata.ttl = cacheTtl(http.header("Cache-Control"));
    metadata.expires = expiry(metadata.ttl);
    time_t now = time(nullptr);
    metadata.fetched = now < 1600000000 ? 0 : uint32_t(now);
    strlcpy(metadata.etag, http.header("ETag").c_str(), sizeof(metadata.etag));
    strlcpy(metadata.modified, http.header("Last-Modified").c_str(), sizeof(metadata.modified));
    http.end();
    if (written != length || !validatePng(temporary, length))
    {
        LittleFS.remove(temporary);
        return false;
    }
    xSemaphoreTake(fsMutex, portMAX_DELAY);
    String target = tilePath(key, ".png");
    LittleFS.remove(target);
    bool saved = LittleFS.rename(temporary, target);
    if (saved)
    {
        File meta = LittleFS.open(tilePath(key, ".meta"), "w");
        saved = meta && meta.write(reinterpret_cast<const uint8_t*>(&metadata), sizeof(metadata)) == sizeof(metadata);
        meta.close();
    }
    xSemaphoreGive(fsMutex);
    if (saved) publishTileChange();
    return saved;
}

void service()
{
    portENTER_CRITICAL(&requestMux);
    bool active = enabled;
    portEXIT_CRITICAL(&requestMux);
    if (!active) return;
    if (!cacheReady || WiFi.status() != WL_CONNECTED) return;
    auto view = currentView();
    int index = 0;
    for (int row = view.firstY; row <= view.lastY; ++row)
    {
        for (int col = view.firstX; col <= view.lastX; ++col, ++index)
        {
            if (index >= 6) return;
            TileKey key {view.zoom, view.tileX(col), row};
            auto& failure = failures[index];
            if (failure.active && failure.key.equals(key) && int32_t(millis() - failure.retryAt) < 0)
                continue;
            xSemaphoreTake(fsMutex, portMAX_DELAY);
            auto metadata = readMetadata(key);
            bool cached = validCache(key, metadata);
            time_t now = time(nullptr);
            if (cached && metadata.expires == UINT32_MAX && now >= 1600000000)
            {
                // Tiles received before NTP synchronization get a finite expiry now.
                metadata.expires = uint32_t(now) + metadata.ttl;
                metadata.fetched = uint32_t(now);
                File meta = LittleFS.open(tilePath(key, ".meta"), "w");
                if (meta)
                    meta.write(reinterpret_cast<const uint8_t*>(&metadata), sizeof(metadata));
                meta.close();
            }
            xSemaphoreGive(fsMutex);
            if (cached && fresh(metadata)) continue;
            bool ok = downloadTile(key, metadata, cached);
            failure.key = key;
            failure.active = !ok;
            failure.retryAt = millis() + 30000;
            return; // One tile per pass so aircraft requests can also run.
        }
    }
}

void setEnabled(bool active)
{
    portENTER_CRITICAL(&requestMux);
    enabled = active;
    portEXIT_CRITICAL(&requestMux);
}

void draw(lgfx::LovyanGFX& display)
{
    auto view = currentView();
    portENTER_CRITICAL(&requestMux);
    uint32_t version = tileVersion;
    portEXIT_CRITICAL(&requestMux);
    int previousCenterX, previousCenterY;
    view.toScreen(drawnLat, drawnLon, previousCenterX, previousCenterY);
    bool changed = retryDecode || version != drawnVersion || previousCenterX != MapProjection::CENTER_X ||
                   previousCenterY != MapProjection::CENTER_Y ||
                   view.radius != drawnRadius || view.zoom != drawnZoom;
    if (changed && cacheReady && backgroundReady && xSemaphoreTake(fsMutex, 0) == pdTRUE)
    {
        bool targetHasTile = false;
        for (int row = view.firstY; row <= view.lastY && !targetHasTile; ++row)
            for (int col = view.firstX; col <= view.lastX; ++col)
            {
                TileKey key {view.zoom, view.tileX(col), row};
                if (validCache(key, readMetadata(key)))
                { targetHasTile = true; break; }
            }

        // A GPS fix can move the view hundreds of kilometres in one update.
        // Keep the last complete background until the first tile for the new
        // location is ready instead of replacing it with an empty canvas.
        if (!targetHasTile && drawnVersion != UINT32_MAX)
        {
            strlcpy(drawnStatus, "Loading GPS map...", sizeof(drawnStatus));
            xSemaphoreGive(fsMutex);
        }
        else
        {
        background.fillScreen(0xE71C);
        visibleTiles = 0;
        retryDecode = false;
        for (int row = view.firstY; row <= view.lastY; ++row)
        {
            for (int col = view.firstX; col <= view.lastX; ++col)
            {
                TileKey key {view.zoom, view.tileX(col), row};
                auto metadata = readMetadata(key);
                if (!validCache(key, metadata)) continue;
                int x = MapProjection::CENTER_X + std::lround((col * 256.0 - view.centerX) * view.scale);
                int y = MapProjection::CENTER_Y - MapProjection::TOP +
                        std::lround((row * 256.0 - view.centerY) * view.scale);
                if (background.drawPngFile(LittleFS, tilePath(key, ".png").c_str(),
                                           x, y, 0, 0, 0, 0, view.scale, view.scale))
                    ++visibleTiles;
                else
                    retryDecode = true;
            }
        }
        background.releasePngMemory();
        drawnVersion = version;
        drawnLat = view.lat; drawnLon = view.lon;
        drawnRadius = view.radius; drawnZoom = view.zoom;
        strlcpy(drawnStatus, visibleTiles ? "" : "Map waiting for data", sizeof(drawnStatus));
        xSemaphoreGive(fsMutex);
        }
    }
    if (backgroundReady && drawnVersion != UINT32_MAX)
        background.pushSprite(&display, 0, MapProjection::TOP);
}

const char* status() { return drawnStatus; }
}
