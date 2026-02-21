#include "vkglobe/OsmProjection.h"

#include <algorithm>
#include <cmath>

namespace vkglobe {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

double clampLat(double latDeg)
{
    return std::clamp(latDeg, -85.05112878, 85.05112878);
}

int tileCountForZoom(int zoom)
{
    return 1 << zoom;
}

double lonToTileX(double lonDeg, int zoom)
{
    const double n = static_cast<double>(tileCountForZoom(zoom));
    return (lonDeg + 180.0) / 360.0 * n;
}

double latToTileY(double latDeg, int zoom)
{
    const double clamped = clampLat(latDeg);
    const double latRad = clamped * (kPi / 180.0);
    const double n = static_cast<double>(tileCountForZoom(zoom));
    return (1.0 - std::asinh(std::tan(latRad)) / kPi) * 0.5 * n;
}

int wrapTileX(int x, int tileCount)
{
    const int wrapped = x % tileCount;
    return wrapped < 0 ? wrapped + tileCount : wrapped;
}

} // namespace vkglobe

