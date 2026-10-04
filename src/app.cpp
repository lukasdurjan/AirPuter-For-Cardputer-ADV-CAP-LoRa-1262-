#include <Arduino.h>
#include <M5Cardputer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h>
#include <Preferences.h>
#include <algorithm>
#include "online_map.h"
#include "outline_map.h"
#include "flight_data.h"
#include "wifi_control.h"
#include "ui_state.h"
#include "generated_version.h"

extern const char* WIFI_SSID;
extern const char* WIFI_PASS;
constexpr double HOME_LAT = 49.0550, HOME_LON = 20.3010;
constexpr int GPS_RX_PIN = 15, MAX_AIRCRAFT = 50;
constexpr uint32_t GPS_MAX_AGE_MS = 5000, REFRESH_MS = 10000;
constexpr size_t MAX_PHOTO_BYTES = 45000;
constexpr int ZOOM_RADII[] = {5, 10, 25, 50, 100, 200};
constexpr char API_USER_AGENT[] = "AirPuter/1.0";
constexpr char PHOTO_USER_AGENT[] = "AirPuter/1.0 (+https://ko-fi.com/lukaslukee)";
constexpr char FIRMWARE_VERSION[] = "AirPuter " AIRPUTER_VERSION;
int zoomIndex = 3, radiusNM = 50;

struct NetworkResult {
    Position center;
    Aircraft aircraft[MAX_AIRCRAFT];
    int count = 0, httpCode = 0;
    bool success = false;
    char error[128] = {};
};

struct RouteRequest {
    uint32_t id = 0;
    char callsign[12] = {};
};

struct RouteResult {
    uint32_t id = 0;
    bool success = false, found = false;
    char callsign[12] = {};
    char originCode[5] = {}, destinationCode[5] = {};
    char origin[20] = {}, destination[20] = {};
    char error[40] = {};
};

struct PhotoRequest {
    uint32_t id = 0;
    char hex[9] = {};
};

struct PhotoResult {
    uint32_t id = 0;
    bool success = false, found = false;
    size_t size = 0;
    char hex[9] = {}, photographer[40] = {}, error[40] = {};
};

HardwareSerial gpsSerial(1);
portMUX_TYPE gpsMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE resultMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE viewMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE routeMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE photoMux = portMUX_INITIALIZER_UNLOCKED;
Position gpsPosition, observerPosition, viewPosition;
Position aircraftRequestCenter;
MapProjection::View projection(HOME_LAT, HOME_LON, 50);
TaskHandle_t networkHandle = nullptr;
NetworkResult inbox, radar, squawkInbox, squawkRadar;
uint32_t inboxVersion = 0, appliedVersion = 0, lastSuccess = 0;
uint32_t squawkInboxVersion = 0, squawkAppliedVersion = 0, squawkLastSuccess = 0;
RouteRequest routeRequest;
RouteResult routeInbox;
uint32_t nextRouteId = 0, consumedRouteId = 0, routeInboxVersion = 0, routeAppliedVersion = 0;
PhotoRequest photoRequest;
PhotoResult photoInbox;
uint32_t nextPhotoId = 0, consumedPhotoId = 0, photoInboxVersion = 0, photoAppliedVersion = 0;
uint8_t photoData[MAX_PHOTO_BYTES] = {};
char statusText[64] = "Connecting WiFi";
M5Canvas canvas(&M5Cardputer.Display);
bool canvasReady = false, followGPS = true, outlineMode = false;
bool trackingSquawk7500 = false, trackedSquawkLive = false;
char trackedSquawkHex[9] = {};
uint16_t autoDimSeconds = 30;
uint32_t lastInputAt = 0;
bool displayDimmed = false;
bool settingsReady = false;
Preferences settings;

UiScreen uiScreen = UiScreen::Map, menuReturn = UiScreen::Map;
int menuIndex = 0, selectedRow = 0, squawkSelectedRow = 0, wifiRow = 0;
int flightOrder[MAX_AIRCRAFT] = {};
double flightDistances[MAX_AIRCRAFT] = {};
char selectedHex[9] = {};
Aircraft detailFlight;
bool detailAvailable = false, detailLive = false;
uint32_t detailUpdated = 0;
bool routeLoading = false, routeAvailable = false;
uint32_t expectedRouteId = 0;
char routeCallsign[12] = {}, routeOriginCode[5] = {}, routeDestinationCode[5] = {};
char routeOrigin[20] = {}, routeDestination[20] = {}, routeStatus[40] = {};
bool photoLoading = false, photoAvailable = false, photoRequested = false, detailPhotoHeld = false;
size_t photoSize = 0;
uint32_t expectedPhotoId = 0;
char photoHex[9] = {}, photoPhotographer[40] = {}, photoStatus[40] = {};
WifiControl::Snapshot wifiView;
WifiControl::Network chosenNetwork;
char wifiPassword[65] = {}, wifiMessage[64] = {};
uint32_t pendingConnectionId = 0;

lgfx::LovyanGFX& screen()
{
    if (canvasReady) return canvas;
    return M5Cardputer.Display;
}

Position readPosition()
{
    portENTER_CRITICAL(&gpsMux);
    Position position = gpsPosition;
    portEXIT_CRITICAL(&gpsMux);
    return position;
}

void gpsTask(void*)
{
    TinyGPSPlus gps;
    for (;;)
    {
        while (gpsSerial.available()) gps.encode(gpsSerial.read());
        Position position;
        if (gps.location.isValid() && gps.location.age() < GPS_MAX_AGE_MS)
        {
            position.lat = gps.location.lat();
            position.lon = gps.location.lng();
            position.fromGPS = true;
        }
        portENTER_CRITICAL(&gpsMux);
        gpsPosition = position;
        portEXIT_CRITICAL(&gpsMux);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

int peekJsonCharacter(WiFiClient& stream)
{
    uint32_t started = millis();
    while (millis() - started < 10000)
    {
        int c = stream.peek();
        if (c < 0) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') { stream.read(); continue; }
        return c;
    }
    return -1;
}

bool downloadAircraft(NetworkResult& result, bool squawk7500 = false)
{
    result.count = 0; result.success = false; result.httpCode = 0; result.error[0] = '\0';
    result.center = readPosition();
    portENTER_CRITICAL(&viewMux);
    int requestRadius = radiusNM;
    if (!squawk7500) result.center = aircraftRequestCenter;
    portEXIT_CRITICAL(&viewMux);
    String url = squawk7500 ? "https://api.adsb.lol/v2/sqk/7500" :
        "https://api.adsb.lol/v2/lat/" + String(result.center.lat, 5) +
        "/lon/" + String(result.center.lon, 5) + "/dist/" + String(requestRadius);
    Serial.println("Request: " + url);
    HTTPClient http;
    http.setUserAgent(API_USER_AGENT);
    http.useHTTP10(true);
    http.setConnectTimeout(10000); http.setTimeout(10000);
    if (!http.begin(url))
    {
        strlcpy(result.error, "HTTP init failed", sizeof(result.error));
        return false;
    }
    http.addHeader("Accept", "application/json");
    result.httpCode = http.GET();
    Serial.printf("HTTP code: %d\n", result.httpCode);
    if (result.httpCode != HTTP_CODE_OK)
    {
        if (result.httpCode > 0)
        {
            auto* stream = http.getStreamPtr(); stream->setTimeout(1000);
            size_t n = stream->readBytes(result.error, sizeof(result.error) - 1);
            result.error[n] = '\0';
        }
        else strlcpy(result.error, HTTPClient::errorToString(result.httpCode).c_str(), sizeof(result.error));
        Serial.printf("HTTP response: %s\n", result.error);
        http.end(); return false;
    }
    WiFiClient& stream = *http.getStreamPtr();
    stream.setTimeout(10000);
    // Parse one aircraft at a time: larger radii cannot fill RAM with the entire response.
    if (!stream.find("\"ac\"") || !stream.find("["))
    {
        strlcpy(result.error, "Missing aircraft array", sizeof(result.error));
        http.end(); return false;
    }
    JsonDocument filter, document;
    const char* fields[] = {"hex", "flight", "r", "t", "lat", "lon", "alt_baro", "gs",
                           "track", "baro_rate", "geom_rate", "squawk", "emergency", "seen_pos"};
    for (const char* field : fields) filter[field] = true;
    while (result.count < MAX_AIRCRAFT)
    {
        int next = peekJsonCharacter(stream);
        if (next == ']') break; // Valid empty response clears the list.
        if (next != '{')
        {
            strlcpy(result.error, "Invalid aircraft JSON", sizeof(result.error));
            http.end(); return false;
        }
        DeserializationError error = deserializeJson(document, stream, DeserializationOption::Filter(filter));
        if (error)
        {
            strlcpy(result.error, error.c_str(), sizeof(result.error));
            http.end(); return false;
        }
        JsonObject item = document.as<JsonObject>();
        if (!item["lat"].isNull() && !item["lon"].isNull())
        {
            Aircraft p;
            p.lat = item["lat"].as<double>(); p.lon = item["lon"].as<double>();
            strlcpy(p.hex, item["hex"] | "", sizeof(p.hex));
            String flight = item["flight"] | ""; flight.trim();
            strlcpy(p.flight, flight.c_str(), sizeof(p.flight));
            strlcpy(p.registration, item["r"] | "", sizeof(p.registration));
            strlcpy(p.type, item["t"] | "", sizeof(p.type));
            strlcpy(p.squawk, item["squawk"] | "", sizeof(p.squawk));
            strlcpy(p.emergency, item["emergency"] | "", sizeof(p.emergency));
            const char* altitudeText = item["alt_baro"] | static_cast<const char*>(nullptr);
            p.onGround = altitudeText && strcmp(altitudeText, "ground") == 0;
            if (item["alt_baro"].is<int>()) p.altitude = item["alt_baro"].as<int>();
            if (!item["gs"].isNull()) p.speed = item["gs"].as<float>();
            if (!item["track"].isNull()) p.track = item["track"].as<float>();
            if (!item["seen_pos"].isNull()) p.seenPosition = item["seen_pos"].as<float>();
            if (item["baro_rate"].is<int>()) p.verticalRate = item["baro_rate"].as<int>();
            else if (item["geom_rate"].is<int>()) p.verticalRate = item["geom_rate"].as<int>();
            if (p.hex[0] && isfinite(p.lat) && isfinite(p.lon) && abs(p.lat) <= 90 && abs(p.lon) <= 180)
                result.aircraft[result.count++] = p;
        }
        if (result.count >= MAX_AIRCRAFT) break;
        next = peekJsonCharacter(stream);
        if (next == ']') break;
        if (next != ',')
        {
            strlcpy(result.error, "Invalid aircraft separator", sizeof(result.error));
            http.end(); return false;
        }
        stream.read();
    }
    http.end();
    Serial.printf("Aircraft loaded: %d\n", result.count);
    return true;
}

bool downloadRoute(const RouteRequest& request, RouteResult& result)
{
    result = RouteResult();
    result.id = request.id;
    strlcpy(result.callsign, request.callsign, sizeof(result.callsign));
    String callsign;
    for (const char* c = request.callsign; *c; ++c)
        if (isalnum(static_cast<unsigned char>(*c))) callsign += char(toupper(static_cast<unsigned char>(*c)));
    if (callsign.length() < 2)
    {
        result.success = true;
        strlcpy(result.error, "No callsign", sizeof(result.error));
        return true;
    }

    String url = "https://vrs-standing-data.adsb.lol/routes/" + callsign.substring(0, 2) +
                 "/" + callsign + ".json";
    HTTPClient http;
    http.setUserAgent(API_USER_AGENT);
    http.useHTTP10(true);
    http.setConnectTimeout(10000); http.setTimeout(10000);
    if (!http.begin(url))
    {
        strlcpy(result.error, "Route HTTP init failed", sizeof(result.error));
        return false;
    }
    http.addHeader("Accept", "application/json");
    int code = http.GET();
    if (code == 404)
    {
        result.success = true;
        strlcpy(result.error, "Route unavailable", sizeof(result.error));
        http.end();
        return true;
    }
    if (code != HTTP_CODE_OK)
    {
        snprintf(result.error, sizeof(result.error), "Route HTTP %d", code);
        http.end();
        return false;
    }

    JsonDocument document;
    DeserializationError error = deserializeJson(document, *http.getStreamPtr());
    if (error)
    {
        strlcpy(result.error, error.c_str(), sizeof(result.error));
        http.end();
        return false;
    }
    JsonArray airports = document["_airports"].as<JsonArray>();
    if (airports.size() >= 2)
    {
        JsonObject origin = airports[0];
        JsonObject destination = airports[airports.size() - 1];
        const char* originCode = origin["iata"] | "";
        const char* destinationCode = destination["iata"] | "";
        if (!originCode[0]) originCode = origin["icao"] | "";
        if (!destinationCode[0]) destinationCode = destination["icao"] | "";
        strlcpy(result.originCode, originCode, sizeof(result.originCode));
        strlcpy(result.destinationCode, destinationCode, sizeof(result.destinationCode));
        const char* originName = origin["location"] | "";
        const char* destinationName = destination["location"] | "";
        if (!originName[0]) originName = origin["name"] | "";
        if (!destinationName[0]) destinationName = destination["name"] | "";
        strlcpy(result.origin, originName, sizeof(result.origin));
        strlcpy(result.destination, destinationName, sizeof(result.destination));
        result.found = result.originCode[0] || result.origin[0];
        result.found = result.found && (result.destinationCode[0] || result.destination[0]);
    }
    result.success = true;
    if (!result.found) strlcpy(result.error, "Route unavailable", sizeof(result.error));
    http.end();
    return true;
}

bool downloadPhoto(const PhotoRequest& request, PhotoResult& result, uint8_t* data)
{
    result = PhotoResult();
    result.id = request.id;
    strlcpy(result.hex, request.hex, sizeof(result.hex));
    String lookupUrl = "https://api.planespotters.net/pub/photos/hex/" + String(request.hex);
    HTTPClient lookup;
    lookup.setUserAgent(PHOTO_USER_AGENT);
    lookup.useHTTP10(true);
    lookup.setConnectTimeout(8000); lookup.setTimeout(8000);
    if (!lookup.begin(lookupUrl))
    { strlcpy(result.error, "Photo lookup failed", sizeof(result.error)); return false; }
    lookup.addHeader("Accept", "application/json");
    int code = lookup.GET();
    if (code != HTTP_CODE_OK)
    {
        snprintf(result.error, sizeof(result.error), "Photo lookup HTTP %d", code);
        lookup.end(); return false;
    }
    String response = lookup.getString();
    JsonDocument document;
    DeserializationError jsonError = deserializeJson(document, response);
    if (jsonError)
    {
        Serial.printf("Photo JSON error: %s; response: %.120s\n", jsonError.c_str(), response.c_str());
        strlcpy(result.error, "Invalid photo response", sizeof(result.error));
        lookup.end(); return false;
    }
    const char* imageUrl = document["photos"][0]["thumbnail"]["src"] | "";
    strlcpy(result.photographer, document["photos"][0]["photographer"] | "", sizeof(result.photographer));
    if (!imageUrl[0])
    {
        result.success = true;
        strlcpy(result.error, "No photo available", sizeof(result.error));
        lookup.end(); return true;
    }
    // PlaneSpotters thumbnails are progressive JPEGs (SOF2), unsupported by the
    // lightweight M5GFX JPEG decoder. Request an equivalent 200 px baseline JPEG.
    String url = "https://wsrv.nl/?url=" + String(imageUrl) + "&w=200&output=jpg";
    lookup.end();

    HTTPClient image;
    image.setUserAgent(PHOTO_USER_AGENT);
    image.useHTTP10(true);
    image.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    image.setConnectTimeout(8000); image.setTimeout(8000);
    if (!image.begin(url))
    { strlcpy(result.error, "Photo download failed", sizeof(result.error)); return false; }
    image.addHeader("Accept", "image/jpeg,image/*;q=0.8");
    image.addHeader("Referer", "https://www.planespotters.net/");
    code = image.GET();
    int length = image.getSize();
    if (code != HTTP_CODE_OK || length <= 0 || length > int(MAX_PHOTO_BYTES))
    {
        snprintf(result.error, sizeof(result.error), "Photo HTTP %d", code);
        image.end(); return false;
    }
    size_t received = image.getStreamPtr()->readBytes(data, length);
    image.end();
    if (received != size_t(length))
    { strlcpy(result.error, "Incomplete photo", sizeof(result.error)); return false; }
    if (received < 4 || data[0] != 0xFF || data[1] != 0xD8 ||
        data[received - 2] != 0xFF || data[received - 1] != 0xD9)
    { strlcpy(result.error, "Invalid JPEG data", sizeof(result.error)); return false; }
    result.size = received;
    result.success = result.found = true;
    return true;
}

void networkTask(void*)
{
    WifiControl::begin(WIFI_SSID, WIFI_PASS);
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    bool attempted = false;
    bool squawkAttempted = false;
    uint32_t lastAttempt = 0;
    uint32_t lastSquawkAttempt = 0;
    int lastRadius = 0;
    Position lastCenter;
    static NetworkResult result; // Large buffers never occupy the worker stack.
    for (;;)
    {
        WifiControl::service();
        if (WiFi.status() != WL_CONNECTED || WifiControl::snapshot().connecting)
        { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)); continue; }
        RouteRequest pendingRoute;
        portENTER_CRITICAL(&routeMux);
        if (routeRequest.id != consumedRouteId)
        { pendingRoute = routeRequest; consumedRouteId = routeRequest.id; }
        portEXIT_CRITICAL(&routeMux);
        if (pendingRoute.id)
        {
            static RouteResult routeResult;
            downloadRoute(pendingRoute, routeResult);
            portENTER_CRITICAL(&routeMux);
            routeInbox = routeResult; ++routeInboxVersion;
            portEXIT_CRITICAL(&routeMux);
        }
        PhotoRequest pendingPhoto;
        portENTER_CRITICAL(&photoMux);
        if (photoRequest.id != consumedPhotoId)
        { pendingPhoto = photoRequest; consumedPhotoId = photoRequest.id; }
        portEXIT_CRITICAL(&photoMux);
        if (pendingPhoto.id)
        {
            static PhotoResult photoResult;
            downloadPhoto(pendingPhoto, photoResult, photoData);
            portENTER_CRITICAL(&photoMux);
            photoInbox = photoResult;
            ++photoInboxVersion;
            portEXIT_CRITICAL(&photoMux);
        }
        OnlineMap::service();
        Position center;
        portENTER_CRITICAL(&viewMux);
        int currentRadius = radiusNM;
        center = aircraftRequestCenter;
        portEXIT_CRITICAL(&viewMux);
        bool changed = currentRadius != lastRadius || center.fromGPS != lastCenter.fromGPS ||
                       distanceNM(lastCenter, center.lat, center.lon) > currentRadius * 0.05;
        uint32_t elapsed = millis() - lastAttempt;
        if (!attempted || elapsed >= REFRESH_MS || (changed && elapsed >= 2000))
        {
            lastAttempt = millis(); attempted = true;
            lastRadius = currentRadius; lastCenter = center;
            result.success = downloadAircraft(result);
            portENTER_CRITICAL(&resultMux);
            inbox = result; ++inboxVersion;
            portEXIT_CRITICAL(&resultMux);
        }
        bool showSquawk = uiScreen == UiScreen::Squawk7500 || trackingSquawk7500;
        if (showSquawk && (!squawkAttempted || millis() - lastSquawkAttempt >= REFRESH_MS))
        {
            lastSquawkAttempt = millis(); squawkAttempted = true;
            static NetworkResult squawkResult;
            squawkResult.success = downloadAircraft(squawkResult, true);
            portENTER_CRITICAL(&resultMux);
            squawkInbox = squawkResult; ++squawkInboxVersion;
            portEXIT_CRITICAL(&resultMux);
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    }
}

void updateFlightOrder()
{
    for (int i = 0; i < radar.count; ++i)
    {
        flightOrder[i] = i;
        flightDistances[i] = distanceNM(observerPosition, radar.aircraft[i].lat, radar.aircraft[i].lon);
    }
    std::sort(flightOrder, flightOrder + radar.count, [](int a, int b) {
        if (flightDistances[a] == flightDistances[b])
            return strcmp(radar.aircraft[a].hex, radar.aircraft[b].hex) < 0;
        return flightDistances[a] < flightDistances[b];
    });
    selectedRow = constrain(selectedRow, 0, max(0, radar.count - 1));
    for (int row = 0; row < radar.count; ++row)
        if (strcmp(radar.aircraft[flightOrder[row]].hex, selectedHex) == 0) selectedRow = row;
    bool holdDetail = uiScreen == UiScreen::Detail ||
        (uiScreen != UiScreen::Map && uiScreen != UiScreen::List && menuReturn == UiScreen::Detail);
    if (!holdDetail && radar.count)
        strlcpy(selectedHex, radar.aircraft[flightOrder[selectedRow]].hex, sizeof(selectedHex));
    detailLive = false;
    for (int i = 0; i < radar.count; ++i)
        if (strcmp(radar.aircraft[i].hex, selectedHex) == 0)
        {
            detailFlight = radar.aircraft[i]; detailAvailable = true; detailLive = true;
            detailUpdated = lastSuccess; break;
        }
}

bool applyNetworkResult()
{
    static NetworkResult result; // Keep the foreground stack small as well.
    bool changed = false;
    portENTER_CRITICAL(&resultMux);
    if (inboxVersion != appliedVersion)
    { result = inbox; appliedVersion = inboxVersion; changed = true; }
    portEXIT_CRITICAL(&resultMux);
    if (!changed) return false;
    if (result.success)
    { radar = result; lastSuccess = millis(); statusText[0] = '\0'; }
    else if (result.httpCode && result.httpCode != HTTP_CODE_OK)
        snprintf(statusText, sizeof(statusText), "HTTP %d - retrying", result.httpCode);
    else strlcpy(statusText, result.error, sizeof(statusText));
    return true;
}

bool applySquawkResult()
{
    static NetworkResult result;
    bool changed = false;
    portENTER_CRITICAL(&resultMux);
    if (squawkInboxVersion != squawkAppliedVersion)
    { result = squawkInbox; squawkAppliedVersion = squawkInboxVersion; changed = true; }
    portEXIT_CRITICAL(&resultMux);
    if (changed && result.success)
    {
        squawkRadar = result; squawkLastSuccess = millis();
        squawkSelectedRow = constrain(squawkSelectedRow, 0, max(0, squawkRadar.count - 1));
        trackedSquawkLive = false;
        if (trackingSquawk7500)
            for (int i = 0; i < squawkRadar.count; ++i)
                if (strcmp(squawkRadar.aircraft[i].hex, trackedSquawkHex) == 0)
                {
                    viewPosition.lat = squawkRadar.aircraft[i].lat;
                    viewPosition.lon = squawkRadar.aircraft[i].lon;
                    viewPosition.fromGPS = false;
                    trackedSquawkLive = true;
                    break;
                }
    }
    return changed;
}

bool applyRouteResult()
{
    RouteResult result;
    bool changed = false;
    portENTER_CRITICAL(&routeMux);
    if (routeInboxVersion != routeAppliedVersion)
    { result = routeInbox; routeAppliedVersion = routeInboxVersion; changed = true; }
    portEXIT_CRITICAL(&routeMux);
    if (!changed || result.id != expectedRouteId) return false;

    routeLoading = false;
    routeAvailable = result.success && result.found;
    strlcpy(routeCallsign, result.callsign, sizeof(routeCallsign));
    strlcpy(routeOriginCode, result.originCode, sizeof(routeOriginCode));
    strlcpy(routeDestinationCode, result.destinationCode, sizeof(routeDestinationCode));
    strlcpy(routeOrigin, result.origin, sizeof(routeOrigin));
    strlcpy(routeDestination, result.destination, sizeof(routeDestination));
    strlcpy(routeStatus, result.error, sizeof(routeStatus));
    return true;
}

bool applyPhotoResult()
{
    PhotoResult result;
    bool changed = false;
    portENTER_CRITICAL(&photoMux);
    if (photoInboxVersion != photoAppliedVersion)
    {
        result = photoInbox;
        photoAppliedVersion = photoInboxVersion;
        changed = true;
    }
    portEXIT_CRITICAL(&photoMux);
    if (!changed || result.id != expectedPhotoId) return false;
    photoLoading = false;
    photoAvailable = result.success && result.found;
    photoSize = photoAvailable ? result.size : 0;
    strlcpy(photoHex, result.hex, sizeof(photoHex));
    strlcpy(photoPhotographer, result.photographer, sizeof(photoPhotographer));
    strlcpy(photoStatus, result.error, sizeof(photoStatus));
    return true;
}

const char* flightName(const Aircraft& p) { return p.flight[0] ? p.flight : p.hex; }

bool textScrolling = false;
uint32_t scrollStarted = 0;

void drawScrollingText(const char* text, int x, int y, int width, bool enabled = true)
{
    auto& d = screen();
    int overflow = d.textWidth(text) - width;
    int offset = 0;
    if (enabled && overflow > 0)
    {
        textScrolling = true;
        // Pause at each end, then move at 24 pixels per second.
        uint32_t travel = (overflow * 1000U + 23) / 24;
        uint32_t phase = (millis() - scrollStarted) % (travel * 2 + 2400);
        if (phase >= 1200 && phase < 1200 + travel)
            offset = min(overflow, int((phase - 1200) * 24 / 1000));
        else if (phase >= 1200 + travel && phase < 2400 + travel)
            offset = overflow;
        else if (phase >= 2400 + travel)
            offset = max(0, overflow - int((phase - 2400 - travel) * 24 / 1000));
    }
    d.setClipRect(x, y, width, 10);
    d.setCursor(x - offset, y); d.print(text);
    d.clearClipRect();
}

void drawHeader(const char* title)
{
    auto& d = screen();
    d.setTextSize(1); d.setTextColor(CYAN, BLACK);
    drawScrollingText(title, 3, 2, 159);
    static int battery = -1;
    static uint32_t lastBatteryRead = 0;
    if (battery < 0 || millis() - lastBatteryRead >= 10000)
    {
        battery = M5Cardputer.Power.getBatteryLevel();
        lastBatteryRead = millis();
    }
    char batteryText[6];
    if (battery < 0) strlcpy(batteryText, "--%", sizeof(batteryText));
    else snprintf(batteryText, sizeof(batteryText), "%d%%", constrain(battery, 0, 100));
    d.setTextColor(battery >= 0 && battery <= 20 ? ORANGE : LIGHTGREY, BLACK);
    d.setCursor(198 - d.textWidth(batteryText), 2); d.print(batteryText);
    uint16_t color = observerPosition.fromGPS ? GREEN : RED;
    d.fillCircle(205, 6, 3, color); d.setTextColor(color, BLACK);
    d.setCursor(212, 2); d.print("GPS");
}

void drawFooter(const char* text)
{
    auto& d = screen(); d.setTextSize(1); d.setTextColor(DARKGREY, BLACK);
    d.setCursor(3, 126); d.print(text);
}

void drawMapScreen()
{
    auto& d = screen();
    projection = MapProjection::View(viewPosition.lat, viewPosition.lon, radiusNM);
    if (outlineMode) drawOutlineMap(d, projection); else OnlineMap::draw(d);
    d.setClipRect(0, 15, 240, 109);
    d.drawCircle(120, 72, 50, 0x2104); d.drawCircle(120, 72, 100, 0x2104);
    int ownX, ownY; projection.toScreen(observerPosition.lat, observerPosition.lon, ownX, ownY);
    d.fillCircle(ownX, ownY, 5, BLACK);
    d.drawLine(ownX - 4, ownY, ownX + 4, ownY, YELLOW);
    d.drawLine(ownX, ownY - 4, ownX, ownY + 4, YELLOW);
    for (int i = 0; i < radar.count; ++i)
    {
        const Aircraft& p = radar.aircraft[i]; int x, y;
        projection.toScreen(p.lat, p.lon, x, y);
        if (x < 0 || x >= 240 || y < 15 || y >= 124) continue;
        if (isfinite(p.track))
        {
            float a = p.track * DEG_TO_RAD, dx = sin(a), dy = -cos(a);
            d.drawLine(x - dx * 3, y - dy * 3, x + dx * 5, y + dy * 5, GREEN);
        }
        d.fillCircle(x, y, 3, BLACK); d.fillCircle(x, y, 2, GREEN);
        if (p.flight[0])
        { d.setTextColor(WHITE, BLACK); d.setCursor(x + 4, y - 3); d.print(p.flight); }
    }
    if (trackingSquawk7500)
    {
        for (int i = 0; i < squawkRadar.count; ++i)
            if (strcmp(squawkRadar.aircraft[i].hex, trackedSquawkHex) == 0)
            {
                int x, y;
                projection.toScreen(squawkRadar.aircraft[i].lat, squawkRadar.aircraft[i].lon, x, y);
                d.drawCircle(x, y, 7, ORANGE);
                d.drawCircle(x, y, 8, ORANGE);
                break;
            }
    }
    d.clearClipRect();
    char title[32];
    if (trackingSquawk7500)
        snprintf(title, sizeof(title), "%dNM TRACK %.8s%s", radiusNM, trackedSquawkHex,
                 trackedSquawkLive ? "" : " LOST");
    else
        snprintf(title, sizeof(title), "%dNM AC:%d %s", radiusNM, radar.count,
                 followGPS ? (observerPosition.fromGPS ? "" : "Poprad") : "PAN");
    drawHeader(title);
    d.setTextColor(CYAN, BLACK); d.setCursor(229, 16); d.print("N");
    d.setTextColor(ORANGE, BLACK); d.setClipRect(0, 124, outlineMode ? 70 : 56, 11); d.setCursor(2, 126);
    if (!wifiView.connected) d.print("No WiFi");
    else if (statusText[0]) d.print(statusText);
    else d.printf("%lus ago", (millis() - lastSuccess) / 1000);
    d.clearClipRect(); d.setTextColor(WHITE, BLACK);
    if (outlineMode) { d.setCursor(80, 126); d.print("NE / (c) GeoNames"); }
    else { d.setCursor(58, 126); d.print("(c) OpenStreetMap contributors"); }

    if (!outlineMode && OnlineMap::status()[0])
    {
        d.fillRect(54, 111, 132, 12, BLACK);
        d.setTextColor(YELLOW, BLACK);
        d.setCursor(60, 113); d.printf("%.20s", OnlineMap::status());
    }
}

void drawListScreen()
{
    auto& d = screen(); char title[32]; snprintf(title, sizeof(title), "FLIGHTS %d (%dNM)", radar.count, radiusNM);
    drawHeader(title);
    if (!radar.count)
    {
        d.setTextColor(WHITE, BLACK); d.setCursor(6, 34); d.print("No aircraft in range");
        d.setCursor(6, 53); d.print(!wifiView.connected ? "Connect WiFi in Opt menu" : statusText);
    }
    int first = selectedRow / 6 * 6;
    for (int row = first; row < min(first + 6, radar.count); ++row)
    {
        int y = 19 + (row - first) * 17;
        uint16_t bg = row == selectedRow ? 0x2104 : BLACK;
        d.fillRect(0, y - 2, 240, 16, bg); d.setTextColor(row == selectedRow ? YELLOW : WHITE, bg);
        const Aircraft& p = radar.aircraft[flightOrder[row]];
        drawScrollingText(flightName(p), 5, y, 76, row == selectedRow);
        d.setCursor(87, y); d.printf("%-5.5s", p.type[0] ? p.type : "--");
        d.setCursor(145, y); d.printf("%6.1f NM", flightDistances[flightOrder[row]]);
    }
    drawFooter("Enter:details Tab:map Opt:menu");
}

void drawSquawk7500Screen()
{
    auto& d = screen();
    char title[32]; snprintf(title, sizeof(title), "SQUAWK7500  %d", squawkRadar.count);
    drawHeader(title);
    if (!squawkRadar.count)
    {
        d.setTextColor(WHITE, BLACK); d.setCursor(6, 34);
        d.print(wifiView.connected ? "No aircraft squawking 7500" : "Connect WiFi in Opt menu");
    }
    int first = squawkSelectedRow / 6 * 6;
    for (int row = first; row < min(first + 6, squawkRadar.count); ++row)
    {
        int y = 19 + (row - first) * 17;
        uint16_t bg = row == squawkSelectedRow ? 0x2104 : BLACK;
        d.fillRect(0, y - 2, 240, 16, bg); d.setTextColor(row == squawkSelectedRow ? YELLOW : WHITE, bg);
        const Aircraft& p = squawkRadar.aircraft[row];
        drawScrollingText(flightName(p), 5, y, 76, row == squawkSelectedRow);
        d.setCursor(87, y); d.printf("%-5.5s", p.type[0] ? p.type : "--");
        d.setCursor(145, y); d.printf("%6.1f NM", distanceNM(observerPosition, p.lat, p.lon));
    }
    drawFooter("Enter:track Tab:map Opt:menu");
}

void drawDetailScreen()
{
    auto& d = screen(); drawHeader(detailAvailable ? flightName(detailFlight) : "FLIGHT");
    if (!detailAvailable) { drawFooter("Esc:list Tab:map"); return; }
    const Aircraft& p = detailFlight;
    d.setTextColor(CYAN, BLACK); d.setCursor(4, 19);
    if (routeLoading)
        d.print(wifiView.connected ? "Route: loading..." : "Route: waiting for WiFi");
    else if (routeAvailable)
    {
        char routeText[80];
        snprintf(routeText, sizeof(routeText), "Route: %s %s > %s %s", routeOriginCode, routeOrigin,
                 routeDestinationCode, routeDestination);
        drawScrollingText(routeText, 4, 19, 232);
    }
    else
        d.print("Route: --");
    d.setTextColor(WHITE, BLACK); d.setCursor(4, 32);
    d.printf("%s  %s  %s", p.hex, p.registration[0] ? p.registration : "--", p.type[0] ? p.type : "--");
    d.setCursor(4, 45); d.printf("Dist %.1f NM  Bearing %.0f deg", distanceNM(observerPosition, p.lat, p.lon),
                               bearingDegrees(observerPosition, p.lat, p.lon));
    d.setCursor(4, 58);
    if (p.onGround) d.print("Altitude: ground");
    else if (p.altitude == INT32_MIN) d.print("Altitude: --");
    else d.printf("Altitude: %d ft", p.altitude);
    d.setCursor(4, 71); d.print("Speed ");
    if (isfinite(p.speed)) d.printf("%.0f kt", p.speed); else d.print("--");
    d.print("  Track "); if (isfinite(p.track)) d.printf("%.0f", p.track); else d.print("--");
    d.setCursor(4, 84); d.print("V/S ");
    if (p.verticalRate != INT32_MIN) d.printf("%d ft/min", p.verticalRate); else d.print("--");
    d.printf("  SQ %s", p.squawk[0] ? p.squawk : "--");
    d.setCursor(4, 97); d.printf("Lat %.4f Lon %.4f", p.lat, p.lon);
    d.setCursor(4, 110);
    if (p.emergency[0] && strcmp(p.emergency, "none") != 0)
    { d.setTextColor(ORANGE, BLACK); d.printf("Emergency: %.23s", p.emergency); }
    else d.printf("%s / data %lus old", detailLive ? "Live" : "Last position",
                  (millis() - detailUpdated) / 1000 + (isfinite(p.seenPosition) ? uint32_t(p.seenPosition) : 0));
    drawFooter("Hold Alt:photo Esc:list Tab:map");
}

void drawPhotoScreen()
{
    auto& d = screen();
    d.fillScreen(BLACK);
    drawHeader(detailAvailable ? flightName(detailFlight) : "AIRCRAFT PHOTO");
    if (photoAvailable && photoSize)
    {
        int x = (240 - 200) / 2;
        int y = 15;
        if (!d.drawJpg(photoData, photoSize, x, y, 200, 108, 0, 0, 1.0f, 1.0f))
        {
            d.setTextColor(ORANGE, BLACK); d.setCursor(55, 54); d.print("Photo decode failed");
        }
        d.fillRect(0, 115, 240, 20, BLACK);
        d.setTextColor(LIGHTGREY, BLACK); d.setCursor(3, 117);
        if (photoPhotographer[0]) d.printf("(c) %.25s / planespotters.net", photoPhotographer);
        else d.print("Photo: planespotters.net");
    }
    else
    {
        d.setTextColor(WHITE, BLACK); d.setCursor(6, 52);
        d.print(photoLoading ? "Loading aircraft photo..." :
                (photoStatus[0] ? photoStatus : "No photo available"));
    }
    drawFooter("Release Alt:details");
}

void drawMenuScreen()
{
    auto& d = screen(); drawHeader("SETTINGS");
    char dimText[24];
    if (autoDimSeconds) snprintf(dimText, sizeof(dimText), "Auto Dim: %us", autoDimSeconds);
    else strlcpy(dimText, "Auto Dim: Off", sizeof(dimText));
    const char* items[] = {outlineMode ? "Map: Outline" : "Map: Tiles", "Wi-Fi networks", "Center on GPS", dimText, "Back"};
    for (int i = 0; i < 5; ++i)
    {
        int y = 21 + i * 20; uint16_t bg = i == menuIndex ? 0x2104 : BLACK;
        d.fillRect(0, y - 3, 240, 18, bg); d.setTextColor(i == menuIndex ? YELLOW : WHITE, bg);
        d.setCursor(7, y); d.print(items[i]);
    }
    drawFooter("Arrows/Enter  Opt/Esc:back");
}

void drawWifiList()
{
    auto& d = screen(); drawHeader(wifiView.scanning ? "WI-FI SCANNING" : "WI-FI NETWORKS");
    if (!wifiView.count)
    {
        d.setTextColor(WHITE, BLACK); d.setCursor(6, 33);
        d.print(wifiView.scanning ? "Scanning nearby networks..." : "No networks found. Press R.");
        d.setCursor(6, 55); d.printf("%.36s", wifiView.error);
    }
    int first = wifiRow / 6 * 6;
    for (int i = first; i < min(first + 6, wifiView.count); ++i)
    {
        int y = 19 + (i - first) * 17; uint16_t bg = i == wifiRow ? 0x2104 : BLACK;
        d.fillRect(0, y - 2, 240, 16, bg); d.setTextColor(i == wifiRow ? YELLOW : WHITE, bg);
        const auto& n = wifiView.networks[i];
        d.setCursor(4, y); d.printf("%c %.23s", n.secured ? '*' : ' ', n.ssid);
        d.setCursor(178, y); d.printf("%d dBm", n.rssi);
    }
    drawFooter("Enter:connect R:scan Esc:menu");
}

void drawWifiPassword()
{
    auto& d = screen(); drawHeader("WI-FI PASSWORD");
    d.setTextColor(WHITE, BLACK); d.setCursor(6, 24); d.printf("%.32s", chosenNetwork.ssid);
    d.setCursor(6, 48); d.print("Password: ");
    int length = strlen(wifiPassword);
    for (int i = 0; i < min(length, 23); ++i) d.print('*');
    d.setCursor(6, 67); d.printf("%d characters", length);
    d.setTextColor(ORANGE, BLACK); d.setCursor(6, 91); d.printf("%.36s", wifiMessage);
    drawFooter("Enter:connect Fn+Esc:cancel");
}

void drawWifiConnecting()
{
    auto& d = screen(); drawHeader("WI-FI");
    d.setTextColor(WHITE, BLACK); d.setCursor(6, 30); d.print("Connecting...");
    d.setCursor(6, 52); d.printf("%.32s", chosenNetwork.ssid);
    drawFooter("Esc:back (connection continues)");
}

void drawUI()
{
    textScrolling = false;
    auto& d = screen(); d.clearClipRect(); d.fillScreen(BLACK); d.setTextSize(1);
    if (uiScreen == UiScreen::Detail && detailPhotoHeld)
        drawPhotoScreen();
    else switch (uiScreen)
    {
        case UiScreen::Map: drawMapScreen(); break;
        case UiScreen::List: drawListScreen(); break;
        case UiScreen::Squawk7500: drawSquawk7500Screen(); break;
        case UiScreen::Detail: drawDetailScreen(); break;
        case UiScreen::Menu: drawMenuScreen(); break;
        case UiScreen::WifiList: drawWifiList(); break;
        case UiScreen::WifiPassword: drawWifiPassword(); break;
        case UiScreen::WifiConnecting: drawWifiConnecting(); break;
    }
    if (canvasReady) canvas.pushSprite(0, 0);
}

void wakeNetwork() { if (networkHandle) xTaskNotifyGive(networkHandle); }

void requestDetailRoute(const Aircraft& aircraft)
{
    routeLoading = false; routeAvailable = false;
    routeCallsign[0] = routeOriginCode[0] = routeDestinationCode[0] = '\0';
    routeOrigin[0] = routeDestination[0] = routeStatus[0] = '\0';
    if (!aircraft.flight[0])
    {
        strlcpy(routeStatus, "No callsign", sizeof(routeStatus));
        return;
    }
    RouteRequest next;
    next.id = ++nextRouteId;
    strlcpy(next.callsign, aircraft.flight, sizeof(next.callsign));
    portENTER_CRITICAL(&routeMux);
    routeRequest = next;
    portEXIT_CRITICAL(&routeMux);
    expectedRouteId = next.id;
    routeLoading = true;
    wakeNetwork();
}

void requestDetailPhoto(const Aircraft& aircraft)
{
    photoLoading = false; photoAvailable = false; photoSize = 0;
    photoHex[0] = photoPhotographer[0] = photoStatus[0] = '\0';
    if (!aircraft.hex[0])
    { strlcpy(photoStatus, "No aircraft identifier", sizeof(photoStatus)); return; }
    PhotoRequest next;
    next.id = ++nextPhotoId;
    strlcpy(next.hex, aircraft.hex, sizeof(next.hex));
    portENTER_CRITICAL(&photoMux);
    photoRequest = next;
    portEXIT_CRITICAL(&photoMux);
    expectedPhotoId = next.id;
    photoLoading = true; photoRequested = true;
    wakeNetwork();
}

void resetDetailPhoto()
{
    photoLoading = false; photoAvailable = false; photoRequested = false;
    photoSize = 0;
    photoHex[0] = photoPhotographer[0] = photoStatus[0] = '\0';
}

void selectFlightRow(int row)
{
    selectedRow = constrain(row, 0, max(0, radar.count - 1));
    if (radar.count) strlcpy(selectedHex, radar.aircraft[flightOrder[selectedRow]].hex, sizeof(selectedHex));
    updateFlightOrder();
}

void startWifiConnection()
{
    pendingConnectionId = WifiControl::connect(chosenNetwork.ssid, wifiPassword);
    memset(wifiPassword, 0, sizeof(wifiPassword)); wifiMessage[0] = '\0';
    uiScreen = UiScreen::WifiConnecting; wakeNetwork();
}

void activateMenuItem()
{
    if (menuIndex == 0)
    {
        outlineMode = !outlineMode;
        if (settingsReady) settings.putBool("outline", outlineMode);
    }
    else if (menuIndex == 1)
    { uiScreen = UiScreen::WifiList; WifiControl::scan(); wakeNetwork(); }
    else if (menuIndex == 2)
    {
        trackingSquawk7500 = false; trackedSquawkHex[0] = '\0';
        followGPS = true; viewPosition = observerPosition; uiScreen = UiScreen::Map; wakeNetwork();
    }
    else if (menuIndex == 3)
    {
        const uint16_t values[] = {30, 60, 120, 0};
        int index = 0;
        while (index < 4 && values[index] != autoDimSeconds) ++index;
        autoDimSeconds = values[(index + 1) % 4];
        if (settingsReady) settings.putUShort("autodim", autoDimSeconds);
        lastInputAt = millis();
    }
    else uiScreen = menuReturn;
}

struct KeyEvents {
    bool up = false, down = false, left = false, right = false;
    bool tab = false, opt = false, enter = false, escape = false, backspace = false;
    bool zoomIn = false, zoomOut = false, rescan = false;
    char text[57] = {};
};

KeyEvents readKeys()
{
    KeyEvents events;
    static uint64_t previous = 0;
    static uint8_t direction = 0;
    static uint32_t repeatAt = 0;
    uint64_t held = 0, fresh = 0;
    auto& keyboard = M5Cardputer.Keyboard;
    for (const auto& point : keyboard.keyList()) held |= uint64_t(1) << (point.y * 14 + point.x);
    fresh = held & ~previous; previous = held;
    int characters = 0;
    uint8_t directions = 0;
    bool passwordScreen = uiScreen == UiScreen::WifiPassword;
    for (const auto& point : keyboard.keyList())
    {
        uint8_t raw = keyboard.getKeyValue(point).value_first;
        bool pressed = fresh & (uint64_t(1) << (point.y * 14 + point.x));
        // Cardputer's printed arrows are on ; (up), . (down), , (left), / (right).
        if (raw == ';') directions |= 1;
        if (raw == '.') directions |= 2;
        if (raw == ',') directions |= 4;
        if (raw == '/') directions |= 8;
        if (!pressed) continue;
        if (raw == KEY_OPT) events.opt = true;
        else if (raw == KEY_TAB) events.tab = true;
        else if (raw == KEY_ENTER) events.enter = true;
        else if (raw == KEY_BACKSPACE) events.backspace = true;
        else if (raw == '`' && (!passwordScreen || M5Cardputer.Keyboard.keysState().fn)) events.escape = true;
        else if (raw >= 32 && raw <= 126)
        {
            events.text[characters++] = M5Cardputer.Keyboard.getKey(point);
            if (raw == 'i') events.zoomIn = true;
            if (raw == 'o') events.zoomOut = true;
            if (raw == 'r') events.rescan = true;
        }
    }
    bool repeat = false;
    if (directions != direction) { direction = directions; repeatAt = millis() + 350; repeat = directions != 0; }
    else if (direction && int32_t(millis() - repeatAt) >= 0) { repeat = true; repeatAt = millis() + 120; }
    if (repeat && !passwordScreen)
    { events.up = directions & 1; events.down = directions & 2; events.left = directions & 4; events.right = directions & 8; }
    return events;
}

bool handleKeys(const KeyEvents& k)
{
    bool changed = k.up || k.down || k.left || k.right || k.tab || k.opt || k.enter || k.escape ||
                   k.zoomIn || k.zoomOut || k.rescan || k.backspace || k.text[0];
    if (!changed) return false;
    lastInputAt = millis();
    if (displayDimmed) { M5Cardputer.Display.setBrightness(128); displayDimmed = false; }
    if (k.opt)
    {
        if (uiScreen == UiScreen::Menu) uiScreen = menuReturn;
        else
        {
            if (uiScreen == UiScreen::Map || uiScreen == UiScreen::List ||
                uiScreen == UiScreen::Squawk7500 || uiScreen == UiScreen::Detail) menuReturn = uiScreen;
            uiScreen = UiScreen::Menu;
        }
        memset(wifiPassword, 0, sizeof(wifiPassword)); return true;
    }
    if (k.escape)
    {
        if (uiScreen == UiScreen::Map)
        {
            trackingSquawk7500 = false; trackedSquawkLive = false; trackedSquawkHex[0] = '\0';
            followGPS = true; viewPosition = observerPosition;
        }
        else uiScreen = escapeDestination(uiScreen, menuReturn);
        memset(wifiPassword, 0, sizeof(wifiPassword)); return true;
    }
    if (uiScreen == UiScreen::WifiPassword)
    {
        size_t n = strlen(wifiPassword);
        if (k.backspace && n) wifiPassword[--n] = '\0';
        for (const char* c = k.text; *c && n < sizeof(wifiPassword) - 1; ++c) wifiPassword[n++] = *c;
        wifiPassword[n] = '\0';
        if (k.enter)
        {
            if (!n) strlcpy(wifiMessage, "Enter password first", sizeof(wifiMessage));
            else startWifiConnection();
        }
        return true;
    }
    if (k.tab)
    { uiScreen = tabDestination(uiScreen); updateFlightOrder(); wakeNetwork(); return true; }
    if (uiScreen == UiScreen::Map)
    {
        int nextZoom = constrain(zoomIndex + (k.zoomOut ? 1 : 0) - (k.zoomIn ? 1 : 0), 0, 5);
        if (nextZoom != zoomIndex)
        {
            zoomIndex = nextZoom; portENTER_CRITICAL(&viewMux);
            radiusNM = ZOOM_RADII[zoomIndex]; portEXIT_CRITICAL(&viewMux); wakeNetwork();
        }
        if (!trackingSquawk7500 && (k.up || k.down || k.left || k.right))
        {
            MapProjection::View view(viewPosition.lat, viewPosition.lon, radiusNM);
            view.fromScreen(120 + (k.right - k.left) * 30, 72 + (k.down - k.up) * 30,
                            viewPosition.lat, viewPosition.lon);
            followGPS = false;
        }
    }
    else if (uiScreen == UiScreen::List)
    {
        selectFlightRow(selectedRow + (k.down ? 1 : 0) - (k.up ? 1 : 0));
        if (k.enter && radar.count)
        {
            Aircraft selected = radar.aircraft[flightOrder[selectedRow]];
            uiScreen = UiScreen::Detail;
            updateFlightOrder();
            requestDetailRoute(selected);
            resetDetailPhoto();
        }
    }
    else if (uiScreen == UiScreen::Squawk7500)
    {
        squawkSelectedRow = constrain(squawkSelectedRow + (k.down ? 1 : 0) - (k.up ? 1 : 0),
                                      0, max(0, squawkRadar.count - 1));
        if (k.enter && squawkRadar.count)
        {
            const Aircraft& tracked = squawkRadar.aircraft[squawkSelectedRow];
            strlcpy(trackedSquawkHex, tracked.hex, sizeof(trackedSquawkHex));
            viewPosition.lat = tracked.lat; viewPosition.lon = tracked.lon; viewPosition.fromGPS = false;
            trackingSquawk7500 = true; trackedSquawkLive = true; followGPS = false;
            uiScreen = UiScreen::Map; wakeNetwork();
        }
    }
    else if (uiScreen == UiScreen::Menu)
    {
        menuIndex = constrain(menuIndex + (k.down - k.up), 0, 4);
        if (k.enter || ((k.left || k.right) && (menuIndex == 0 || menuIndex == 3))) activateMenuItem();
    }
    else if (uiScreen == UiScreen::WifiList)
    {
        wifiRow = constrain(wifiRow + (k.down ? 1 : 0) - (k.up ? 1 : 0),
                            0, max(0, wifiView.count - 1));
        if (k.rescan) { WifiControl::scan(); wakeNetwork(); }
        if (k.enter && wifiView.count)
        {
            chosenNetwork = wifiView.networks[wifiRow];
            memset(wifiPassword, 0, sizeof(wifiPassword)); wifiMessage[0] = '\0';
            if (chosenNetwork.secured) uiScreen = UiScreen::WifiPassword;
            else startWifiConnection();
        }
    }
    return true;
}

void drawSplash()
{
    auto& d = screen();
    constexpr char title[] = "Coffee flies the World";
    constexpr char address[] = "https://ko-fi.com/lukaslukee";
    d.fillScreen(BLACK);
    d.drawRoundRect(12, 25, 216, 85, 8, 0x39E7);
    d.setTextSize(1);
    d.setTextColor(YELLOW, BLACK);
    d.setCursor((240 - d.textWidth(title)) / 2, 48);
    d.print(title);
    d.drawLine(42, 64, 198, 64, 0x39E7);
    d.setTextColor(CYAN, BLACK);
    d.setCursor((240 - d.textWidth(address)) / 2, 78);
    d.print(address);
    if (canvasReady) canvas.pushSprite(0, 0);
    delay(2500);
}

void setup()
{
    M5Cardputer.begin(M5.config(), true); Serial.begin(115200); Serial.println(FIRMWARE_VERSION);
    canvas.setColorDepth(8); canvasReady = canvas.createSprite(240, 135) != nullptr;
    drawSplash();
    settingsReady = settings.begin("airputer-ui", false);
    if (settingsReady)
    {
        outlineMode = settings.getBool("outline", false);
        autoDimSeconds = settings.getUShort("autodim", 30);
        if (autoDimSeconds != 30 && autoDimSeconds != 60 && autoDimSeconds != 120 && autoDimSeconds != 0)
            autoDimSeconds = 30;
    }
    M5Cardputer.Display.setBrightness(128); lastInputAt = millis();
    OnlineMap::begin(); OnlineMap::request(viewPosition.lat, viewPosition.lon, radiusNM);
    OnlineMap::setEnabled(!outlineMode);
    gpsSerial.setRxBufferSize(2048); gpsSerial.begin(115200, SERIAL_8N1, GPS_RX_PIN, -1);
    if (xTaskCreate(gpsTask, "LoraCap GPS", 4096, nullptr, 1, nullptr) != pdPASS)
        Serial.println("GPS task failed; using Poprad");
    if (xTaskCreate(networkTask, "ADSB network", 12288, nullptr, 1, &networkHandle) != pdPASS)
        strlcpy(statusText, "Network task failed", sizeof(statusText));
    drawUI();
}

void loop()
{
    M5Cardputer.update();
    observerPosition = readPosition();
    if (followGPS) viewPosition = observerPosition;
    bool changed = applyNetworkResult();
    changed |= applySquawkResult();
    changed |= applyRouteResult();
    changed |= applyPhotoResult();
    char selectedSSID[33] = {};
    if (wifiView.count && wifiRow >= 0 && wifiRow < wifiView.count)
        strlcpy(selectedSSID, wifiView.networks[wifiRow].ssid, sizeof(selectedSSID));
    wifiView = WifiControl::snapshot();
    wifiRow = constrain(wifiRow, 0, max(0, wifiView.count - 1));
    for (int i = 0; i < wifiView.count; ++i)
        if (strcmp(selectedSSID, wifiView.networks[i].ssid) == 0) wifiRow = i;
    static uint32_t lastOrder = 0;
    if (changed || millis() - lastOrder >= 1000) { updateFlightOrder(); lastOrder = millis(); }
    bool keysChanged = handleKeys(readKeys());
    if (keysChanged) scrollStarted = millis();
    changed |= keysChanged;
    bool photoHeldNow = uiScreen == UiScreen::Detail && M5Cardputer.Keyboard.keysState().alt;
    if (photoHeldNow != detailPhotoHeld)
    {
        detailPhotoHeld = photoHeldNow;
        if (photoHeldNow && !photoRequested) requestDetailPhoto(detailFlight);
        lastInputAt = millis();
        if (displayDimmed) { M5Cardputer.Display.setBrightness(128); displayDimmed = false; }
        changed = true;
    }
    if (!displayDimmed && autoDimSeconds && millis() - lastInputAt >= uint32_t(autoDimSeconds) * 1000)
    { M5Cardputer.Display.setBrightness(16); displayDimmed = true; }
    if (uiScreen == UiScreen::WifiConnecting && wifiView.connectionId == pendingConnectionId && !wifiView.connecting)
    {
        if (wifiView.connected && strcmp(wifiView.ssid, chosenNetwork.ssid) == 0)
        { uiScreen = UiScreen::Menu; changed = true; }
        else if (wifiView.error[0])
        {
            strlcpy(wifiMessage, wifiView.error, sizeof(wifiMessage));
            uiScreen = chosenNetwork.secured ? UiScreen::WifiPassword : UiScreen::WifiList;
            changed = true;
        }
    }
    OnlineMap::setEnabled(uiScreen == UiScreen::Map && !outlineMode);
    portENTER_CRITICAL(&viewMux);
    aircraftRequestCenter = viewPosition;
    portEXIT_CRITICAL(&viewMux);
    if (OnlineMap::request(viewPosition.lat, viewPosition.lon, radiusNM)) wakeNetwork();
    static uint32_t lastDraw = 0;
    if (changed || millis() - lastDraw >= (textScrolling ? 40U : 1000U)) { lastDraw = millis(); drawUI(); }
    delay(20);
}
