#pragma once

namespace vkglobe {

double clampLat(double latDeg);
int tileCountForZoom(int zoom);
double lonToTileX(double lonDeg, int zoom);
double latToTileY(double latDeg, int zoom);
int wrapTileX(int x, int tileCount);

} // namespace vkglobe

