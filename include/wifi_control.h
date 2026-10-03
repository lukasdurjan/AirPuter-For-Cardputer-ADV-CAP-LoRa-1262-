#pragma once
#include <stdint.h>

namespace WifiControl {
constexpr int MAX_NETWORKS = 20;
struct Network { char ssid[33] = {}; int rssi = 0; bool secured = false; };
struct Snapshot {
    Network networks[MAX_NETWORKS];
    int count = 0;
    bool scanning = false, connecting = false, connected = false;
    uint32_t connectionId = 0;
    char ssid[33] = {}, ip[16] = {}, error[64] = {};
};
// All radio operations run on the network worker, keeping the UI responsive.
void begin(const char* defaultSSID, const char* defaultPassword);
void service();
void scan();
uint32_t connect(const char* ssid, const char* password);
Snapshot snapshot();
}
