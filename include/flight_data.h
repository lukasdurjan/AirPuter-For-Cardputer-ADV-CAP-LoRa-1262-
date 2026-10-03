#pragma once
#include <stdint.h>
#include <cmath>
#include <algorithm>

struct Position {
    double lat = 49.0550, lon = 20.3010;
    bool fromGPS = false;
};

struct Aircraft {
    char hex[9] = {}, flight[12] = {}, registration[16] = {}, type[12] = {};
    char squawk[8] = {}, emergency[24] = {};
    double lat = 0, lon = 0;
    float track = NAN, speed = NAN, seenPosition = NAN;
    int altitude = INT32_MIN, verticalRate = INT32_MIN;
    bool onGround = false;
};

inline double distanceNM(const Position& origin, double lat, double lon)
{
    constexpr double radians = 0.017453292519943295;
    double a = origin.lat * radians, b = lat * radians;
    double dlat = b - a, dlon = (lon - origin.lon) * radians;
    double h = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(a) * std::cos(b) * std::sin(dlon / 2) * std::sin(dlon / 2);
    return 3440.069546436 * 2 * std::asin(std::sqrt(std::max(0.0, std::min(1.0, h))));
}

inline double bearingDegrees(const Position& origin, double lat, double lon)
{
    constexpr double radians = 0.017453292519943295;
    double a = origin.lat * radians, b = lat * radians, d = (lon - origin.lon) * radians;
    double angle = std::atan2(std::sin(d) * std::cos(b),
        std::cos(a) * std::sin(b) - std::sin(a) * std::cos(b) * std::cos(d)) / radians;
    return std::fmod(angle + 360.0, 360.0);
}
