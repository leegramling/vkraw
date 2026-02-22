#include "vkglobe/OsmTileManager.h"

#include "vkglobe/OsmProjection.h"
#include "vkglobe/OsmTileFetcher.h"

#include <vsg/all.h>
#include <vsg/io/read.h>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace vkglobe {

namespace {
constexpr double kPi = 3.14159265358979323846;

bool intersectEllipsoidFromEyeToCenter(const vsg::dvec3& eyeWorld, const vsg::dmat4& globeRotation, double equatorialRadius, double polarRadius,
                                       vsg::dvec3& hitWorld)
{
    const vsg::dvec3 rayDirWorld = vsg::normalize(-eyeWorld);
    const vsg::dmat4 invRot = vsg::inverse(globeRotation);
    const vsg::dvec4 o4 = invRot * vsg::dvec4(eyeWorld.x, eyeWorld.y, eyeWorld.z, 1.0);
    const vsg::dvec4 d4 = invRot * vsg::dvec4(rayDirWorld.x, rayDirWorld.y, rayDirWorld.z, 0.0);
    const vsg::dvec3 o(o4.x, o4.y, o4.z);
    const vsg::dvec3 d = vsg::normalize(vsg::dvec3(d4.x, d4.y, d4.z));

    const double a2 = equatorialRadius * equatorialRadius;
    const double b2 = polarRadius * polarRadius;

    const double A = (d.x * d.x + d.y * d.y) / a2 + (d.z * d.z) / b2;
    const double B = 2.0 * ((o.x * d.x + o.y * d.y) / a2 + (o.z * d.z) / b2);
    const double C = (o.x * o.x + o.y * o.y) / a2 + (o.z * o.z) / b2 - 1.0;

    const double disc = B * B - 4.0 * A * C;
    if (disc < 0.0) return false;
    const double sqrtDisc = std::sqrt(disc);
    const double t0 = (-B - sqrtDisc) / (2.0 * A);
    const double t1 = (-B + sqrtDisc) / (2.0 * A);
    double t = t0;
    if (t <= 0.0) t = t1;
    if (t <= 0.0) return false;

    const vsg::dvec3 localHit = o + d * t;
    const vsg::dvec4 hw = globeRotation * vsg::dvec4(localHit.x, localHit.y, localHit.z, 1.0);
    hitWorld = vsg::dvec3(hw.x, hw.y, hw.z);
    return true;
}

vsg::ref_ptr<vsg::Data> createMissingTileDebugImage()
{
    constexpr uint32_t w = 64;
    constexpr uint32_t h = 64;
    auto tex = vsg::ubvec4Array2D::create(w, h, vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM});
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            const bool a = ((x / 8) + (y / 8)) % 2 == 0;
            tex->set(x, y, a ? vsg::ubvec4(255, 0, 255, 255) : vsg::ubvec4(0, 255, 255, 255));
        }
    }
    tex->dirty();
    return tex;
}

} // namespace

bool TileKey::operator<(const TileKey& rhs) const
{
    if (z != rhs.z) return z < rhs.z;
    if (x != rhs.x) return x < rhs.x;
    return y < rhs.y;
}

OsmTileManager::OsmTileManager(vsg::ref_ptr<vsg::Options> options, Config cfg) :
    options_(std::move(options)),
    cfg_(std::move(cfg))
{
}

void OsmTileManager::setEnabled(bool enabled)
{
    enabled_ = enabled;
    if (!enabled_)
    {
        active_ = false;
        visibleTiles_.clear();
    }
}

void OsmTileManager::setMaxZoom(int maxZoom)
{
    cfg_.maxZoom = std::clamp(maxZoom, cfg_.minZoom, 22);
}

void OsmTileManager::setActivationAltitudes(double enableAltitudeFt, double disableAltitudeFt)
{
    cfg_.enableAltitudeFt = std::max(0.0, enableAltitudeFt);
    cfg_.disableAltitudeFt = std::max(cfg_.enableAltitudeFt + 1.0, disableAltitudeFt);
}

void OsmTileManager::update(const vsg::dvec3& eyeWorld, const vsg::dmat4& globeRotation, double equatorialRadiusFt, double polarRadiusFt)
{
    if (!enabled_) return;

    double latDeg = 0.0;
    double lonDeg = 0.0;
    double altitudeFt = 0.0;
    if (!computeSubCameraGeo(eyeWorld, globeRotation, equatorialRadiusFt, polarRadiusFt, latDeg, lonDeg, altitudeFt)) return;

    currentLatDeg_ = latDeg;
    currentLonDeg_ = lonDeg;
    currentAltitudeFt_ = altitudeFt;
    if (active_)
    {
        if (altitudeFt >= cfg_.disableAltitudeFt) active_ = false;
    }
    else
    {
        if (altitudeFt <= cfg_.enableAltitudeFt) active_ = true;
    }

    if (!active_)
    {
        currentZoom_ = 0;
        visibleTiles_.clear();
        return;
    }

    const int zoom = chooseZoomForAltitude(altitudeFt);
    currentZoom_ = zoom;
    requestVisibleTiles(latDeg, lonDeg, zoom);
    fetchAndDecodeBudgeted();
}

bool OsmTileManager::computeSubCameraGeo(const vsg::dvec3& eyeWorld, const vsg::dmat4& globeRotation, double equatorialRadiusFt,
                                         double polarRadiusFt, double& outLatDeg, double& outLonDeg, double& outAltitudeFt) const
{
    vsg::dvec3 hitWorld;
    if (!intersectEllipsoidFromEyeToCenter(eyeWorld, globeRotation, equatorialRadiusFt, polarRadiusFt, hitWorld)) return false;

    outAltitudeFt = vsg::length(eyeWorld - hitWorld);

    const vsg::dmat4 invRot = vsg::inverse(globeRotation);
    const vsg::dvec4 hl4 = invRot * vsg::dvec4(hitWorld.x, hitWorld.y, hitWorld.z, 1.0);
    const vsg::dvec3 hl(hl4.x, hl4.y, hl4.z);

    const double xy = std::sqrt(hl.x * hl.x + hl.y * hl.y);
    outLatDeg = std::atan2(hl.z, std::max(1e-9, xy)) * (180.0 / kPi);
    outLonDeg = std::atan2(hl.x, -hl.y) * (180.0 / kPi);
    return true;
}

int OsmTileManager::chooseZoomForAltitude(double altitudeFt) const
{
    if (altitudeFt <= 500.0) return std::clamp(cfg_.maxZoom, cfg_.minZoom, cfg_.maxZoom);
    if (altitudeFt <= 1000.0) return std::clamp(cfg_.maxZoom - 1, cfg_.minZoom, cfg_.maxZoom);
    if (altitudeFt <= 3000.0) return std::clamp(cfg_.maxZoom - 2, cfg_.minZoom, cfg_.maxZoom);
    if (altitudeFt <= 12000.0) return std::clamp(cfg_.maxZoom - 4, cfg_.minZoom, cfg_.maxZoom);
    if (altitudeFt <= 50000.0) return std::clamp(cfg_.maxZoom - 6, cfg_.minZoom, cfg_.maxZoom);
    return std::clamp(cfg_.maxZoom - 8, cfg_.minZoom, cfg_.maxZoom);
}

void OsmTileManager::requestVisibleTiles(double latDeg, double lonDeg, int zoom)
{
    const int tiles = tileCountForZoom(zoom);
    const int centerX = static_cast<int>(std::floor(lonToTileX(lonDeg, zoom)));
    const int centerY = static_cast<int>(std::floor(latToTileY(latDeg, zoom)));
    const int radius = std::max(1, cfg_.tileRadius);
    currentCenterTileX_ = centerX;
    currentCenterTileY_ = centerY;
    currentTileRadius_ = radius;
    visibleTiles_.clear();
    for (int oy = -radius; oy <= radius; ++oy)
    {
        for (int ox = -radius; ox <= radius; ++ox)
        {
            const int tx = wrapTileX(centerX + ox, tiles);
            const int ty = std::clamp(centerY + oy, 0, tiles - 1);
            visibleTiles_.insert(TileKey{zoom, tx, ty});
        }
    }
}

void OsmTileManager::fetchAndDecodeBudgeted()
{
    int fetchedThisFrame = 0;
    for (const TileKey& key : visibleTiles_)
    {
        auto& entry = tileCache_[key];
        if (entry.loaded) continue;
        if (fetchedThisFrame >= cfg_.maxFetchPerFrame) break;

        const std::filesystem::path cacheFile = cfg_.cacheRoot / std::to_string(key.z) / std::to_string(key.x) / (std::to_string(key.y) + ".png");
        if (!downloadOsmTileIfNeeded(key.z, key.x, key.y, cacheFile))
        {
            entry.image = createMissingTileDebugImage();
            entry.loaded = true;
            ++fetchedThisFrame;
            std::cerr << "[OSM] fetch failed z=" << key.z << " x=" << key.x << " y=" << key.y
                      << " (using debug tile)\n";
            continue;
        }
        entry.fetched = true;

        auto data = vsg::read_cast<vsg::Data>(cacheFile.string(), options_);
        if (!data)
        {
            entry.image = createMissingTileDebugImage();
            entry.loaded = true;
            ++fetchedThisFrame;
            std::cerr << "[OSM] decode failed for '" << cacheFile.string() << "' (using debug tile)\n";
            continue;
        }
        entry.image = data;
        entry.loaded = true;
        ++fetchedThisFrame;
    }
}

std::vector<std::pair<TileKey, vsg::ref_ptr<vsg::Data>>> OsmTileManager::loadedVisibleTiles() const
{
    std::vector<std::pair<TileKey, vsg::ref_ptr<vsg::Data>>> tiles;
    tiles.reserve(visibleTiles_.size());
    for (const TileKey& key : visibleTiles_)
    {
        auto it = tileCache_.find(key);
        if (it == tileCache_.end()) continue;
        if (!it->second.loaded || !it->second.image) continue;
        tiles.emplace_back(key, it->second.image);
    }
    return tiles;
}

std::vector<TileSample> OsmTileManager::currentTileWindow() const
{
    std::vector<TileSample> window;
    if (currentZoom_ <= 0) return window;

    const int tiles = tileCountForZoom(currentZoom_);
    const int radius = std::max(1, currentTileRadius_);
    window.reserve(static_cast<size_t>((2 * radius + 1) * (2 * radius + 1)));
    for (int oy = -radius; oy <= radius; ++oy)
    {
        for (int ox = -radius; ox <= radius; ++ox)
        {
            const int tx = wrapTileX(currentCenterTileX_ + ox, tiles);
            const int ty = std::clamp(currentCenterTileY_ + oy, 0, tiles - 1);
            const TileKey key{currentZoom_, tx, ty};
            TileSample sample{};
            sample.key = key;
            sample.ox = ox;
            sample.oy = oy;
            auto it = tileCache_.find(key);
            if (it != tileCache_.end() && it->second.loaded && it->second.image)
            {
                sample.loaded = true;
                sample.image = it->second.image;
            }
            window.push_back(sample);
        }
    }
    return window;
}

} // namespace vkglobe
