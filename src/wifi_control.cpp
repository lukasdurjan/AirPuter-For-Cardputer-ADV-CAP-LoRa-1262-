#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <algorithm>
#include "wifi_control.h"

namespace WifiControl {
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Snapshot published;
Snapshot worker;
struct Request {
    bool scan = false, connect = false;
    uint32_t id = 0;
    char ssid[33] = {}, password[65] = {};
} request;
char activeSSID[33] = {}, activePassword[65] = {};
uint32_t connectStarted = 0;
bool savingPending = false;
bool scanRunning = false;
uint32_t nextConnectionId = 0;

void publish()
{
    portENTER_CRITICAL(&mux);
    published = worker;
    // Retain the pending indication if a command arrived during a radio operation.
    if (request.scan) published.scanning = true;
    if (request.connect)
    {
        published.connecting = true;
        published.connectionId = request.id;
        strlcpy(published.ssid, request.ssid, sizeof(published.ssid));
        published.error[0] = '\0';
    }
    portEXIT_CRITICAL(&mux);
}

Snapshot snapshot()
{
    portENTER_CRITICAL(&mux);
    Snapshot value = published;
    portEXIT_CRITICAL(&mux);
    return value;
}

void scan()
{
    portENTER_CRITICAL(&mux);
    request.scan = true;
    published.scanning = true;
    published.error[0] = '\0';
    portEXIT_CRITICAL(&mux);
}

uint32_t connect(const char* ssid, const char* password)
{
    portENTER_CRITICAL(&mux);
    strlcpy(request.ssid, ssid, sizeof(request.ssid));
    strlcpy(request.password, password, sizeof(request.password));
    request.connect = true;
    request.id = ++nextConnectionId;
    published.connectionId = request.id;
    published.connecting = true;
    published.error[0] = '\0';
    uint32_t id = request.id;
    portEXIT_CRITICAL(&mux);
    return id;
}

void startConnection(const char* ssid, const char* password)
{
    if (scanRunning) { WiFi.scanDelete(); scanRunning = false; }
    worker.scanning = false;
    strlcpy(activeSSID, ssid, sizeof(activeSSID));
    strlcpy(activePassword, password, sizeof(activePassword));
    worker.error[0] = '\0';
    worker.connecting = true;
    worker.connected = false;
    strlcpy(worker.ssid, ssid, sizeof(worker.ssid));
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    WiFi.begin(ssid, password);
    connectStarted = millis();
    savingPending = true;
}

void begin(const char* defaultSSID, const char* defaultPassword)
{
    WiFi.mode(WIFI_STA);
    Preferences prefs;
    String ssid(defaultSSID), password(defaultPassword);
    if (prefs.begin("airputer-wifi", true))
    {
        ssid = prefs.getString("ssid", defaultSSID);
        password = prefs.getString("password", defaultPassword);
        prefs.end();
    }
    startConnection(ssid.c_str(), password.c_str());
    publish();
}

void service()
{
    Request next;
    portENTER_CRITICAL(&mux);
    next = request;
    request.scan = request.connect = false;
    memset(request.password, 0, sizeof(request.password));
    portEXIT_CRITICAL(&mux);
    if (next.connect)
    {
        startConnection(next.ssid, next.password);
        worker.connectionId = next.id;
        memset(next.password, 0, sizeof(next.password));
    }
    if (next.scan && !scanRunning && !worker.connecting)
    {
        WiFi.scanDelete();
        int16_t result = WiFi.scanNetworks(true, false);
        scanRunning = result == WIFI_SCAN_RUNNING || result >= 0;
        worker.scanning = scanRunning;
        worker.error[0] = '\0';
        if (!scanRunning) strlcpy(worker.error, "WiFi scan failed", sizeof(worker.error));
    }
    else if (next.scan && worker.connecting)
    {
        portENTER_CRITICAL(&mux);
        request.scan = true;
        portEXIT_CRITICAL(&mux);
    }
    if (scanRunning)
    {
        int16_t result = WiFi.scanComplete();
        if (result >= 0)
        {
            worker.count = 0;
            for (int i = 0; i < result; ++i)
            {
                String ssid = WiFi.SSID(i);
                if (ssid.isEmpty()) continue;
                Network network;
                strlcpy(network.ssid, ssid.c_str(), sizeof(network.ssid));
                network.rssi = WiFi.RSSI(i);
                network.secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
                int index = -1;
                for (int j = 0; j < worker.count; ++j)
                    if (strcmp(worker.networks[j].ssid, network.ssid) == 0) index = j;
                if (index >= 0)
                {
                    if (network.rssi > worker.networks[index].rssi) worker.networks[index] = network;
                }
                else if (worker.count < MAX_NETWORKS)
                    worker.networks[worker.count++] = network;
                else
                {
                    int weakest = 0;
                    for (int j = 1; j < worker.count; ++j)
                        if (worker.networks[j].rssi < worker.networks[weakest].rssi) weakest = j;
                    if (network.rssi > worker.networks[weakest].rssi) worker.networks[weakest] = network;
                }
            }
            std::sort(worker.networks, worker.networks + worker.count,
                [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
            WiFi.scanDelete();
            scanRunning = worker.scanning = false;
        }
        else if (result != WIFI_SCAN_RUNNING)
        {
            scanRunning = worker.scanning = false;
            strlcpy(worker.error, "WiFi scan failed", sizeof(worker.error));
        }
    }
    worker.connected = WiFi.status() == WL_CONNECTED &&
        (!worker.connecting || (millis() - connectStarted >= 250 && WiFi.SSID() == activeSSID));
    if (worker.connected)
    {
        strlcpy(worker.ssid, WiFi.SSID().c_str(), sizeof(worker.ssid));
        strlcpy(worker.ip, WiFi.localIP().toString().c_str(), sizeof(worker.ip));
        worker.connecting = false;
        WiFi.setAutoReconnect(true);
        if (savingPending)
        {
            Preferences prefs;
            if (prefs.begin("airputer-wifi", false))
            {
                prefs.putString("ssid", activeSSID);
                prefs.putString("password", activePassword);
                prefs.end();
            }
            memset(activePassword, 0, sizeof(activePassword));
            savingPending = false;
        }
    }
    else if (worker.connecting && millis() - connectStarted >= 15000)
    {
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
        worker.connecting = false;
        savingPending = false;
        memset(activePassword, 0, sizeof(activePassword));
        strlcpy(worker.error, "Connection failed. Check password.", sizeof(worker.error));
    }
    publish();
}
}
