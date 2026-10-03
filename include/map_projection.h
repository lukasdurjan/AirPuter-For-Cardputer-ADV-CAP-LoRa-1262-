#pragma once
#include <cmath>
#include <algorithm>

namespace MapProjection {
constexpr double kPi = 3.14159265358979323846;
constexpr double MAX_LAT = 85.05112878;
constexpr int LEFT = 0, TOP = 15, WIDTH = 240, HEIGHT = 109;
constexpr int CENTER_X = 120, CENTER_Y = 72;

inline double wrappedLongitude(double lon)
{
    return std::fmod(lon + 540.0, 360.0) - 180.0;
}

inline double worldX(double lon, int zoom)
{
    return (wrappedLongitude(lon) + 180.0) / 360.0 * (256u << zoom);
}

inline double worldY(double lat, int zoom)
{
    lat = std::max(-MAX_LAT, std::min(MAX_LAT, lat));
    double radians = lat * kPi / 180.0;
    return (1.0 - std::asinh(std::tan(radians)) / kPi) / 2.0 * (256u << zoom);
}

struct View {
    double lat, lon;
    int radius;
    int zoom;
    double scale, centerX, centerY;
    int firstX, lastX, firstY, lastY;

    View(double latitude, double longitude, int radiusNM)
        : lat(std::max(-MAX_LAT, std::min(MAX_LAT, latitude))),
          lon(wrappedLongitude(longitude)), radius(radiusNM)
    {
        double desiredMetersPerPixel = radiusNM * 1852.0 / 100.0;
        double circumference = 40075016.686 * std::cos(lat * kPi / 180.0);
        zoom = std::max(0, std::min(18, int(std::lround(
            std::log2(circumference / (256.0 * desiredMetersPerPixel))))));
        scale = circumference / (256u << zoom) / desiredMetersPerPixel;
        centerX = worldX(lon, zoom);
        centerY = worldY(lat, zoom);
        firstX = int(std::floor((centerX - CENTER_X / scale) / 256.0));
        lastX = int(std::floor((centerX + (WIDTH - 1 - CENTER_X) / scale) / 256.0));
        firstY = std::max(0, int(std::floor((centerY - (CENTER_Y - TOP) / scale) / 256.0)));
        lastY = std::min((1 << zoom) - 1, int(std::floor(
            (centerY + (TOP + HEIGHT - 1 - CENTER_Y) / scale) / 256.0)));
    }

    int tileX(int column) const
    {
        int count = 1 << zoom;
        return (column % count + count) % count;
    }

    void fromScreen(double x, double y, double& latitude, double& longitude) const
    {
        double worldSize = 256u << zoom;
        double px = centerX + (x - CENTER_X) / scale;
        double py = std::max(0.0, std::min(worldSize, centerY + (y - CENTER_Y) / scale));
        longitude = wrappedLongitude(px / worldSize * 360.0 - 180.0);
        latitude = std::atan(std::sinh(kPi * (1.0 - 2.0 * py / worldSize))) * 180.0 / kPi;
    }

    void toScreen(double latitude, double longitude, int& x, int& y) const
    {
        double worldSize = 256u << zoom;
        double dx = worldX(longitude, zoom) - centerX;
        if (dx > worldSize / 2) dx -= worldSize;
        if (dx < -worldSize / 2) dx += worldSize;
        x = CENTER_X + std::lround(dx * scale);
        y = CENTER_Y + std::lround((worldY(latitude, zoom) - centerY) * scale);
    }
};
}
