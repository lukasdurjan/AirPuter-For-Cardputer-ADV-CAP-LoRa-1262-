#pragma once
#include <M5GFX.h>
#include "map_projection.h"

namespace OnlineMap {
// Main thread owns request/render; the network worker owns service.
void begin();
bool request(double latitude, double longitude, int radiusNM);
void service();
void setEnabled(bool enabled);
void draw(lgfx::LovyanGFX& display);
const char* status();
}
