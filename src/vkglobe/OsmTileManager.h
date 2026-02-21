#pragma once

#include <vsg/core/Inherit.h>
#include <vsg/core/Data.h>
#include <vsg/core/Object.h>
#include <vsg/io/Options.h>

#include <filesystem>
#include <map>
#include <set>
#include <vector>

namespace vkglobe {

struct TileKey
{
    int z = 0;
    int x = 0;
    int y = 0;

    bool operator<(const TileKey& rhs) const;
};

struct TileEntry
{
    bool fetched = false;
    bool loaded = false;
    vsg::ref_ptr<vsg::Data> image;
};

class OsmTileManager : public vsg::Inherit<vsg::Object, OsmTileManager>
{
public:
    struct Config
    {
        std::filesystem::path cacheRoot = "cache/osm";
        int maxFetchPerFrame = 4;
        int minZoom = 1;
        int maxZoom = 19;
        double enableAltitudeFt = 10000.0;
        double disableAltitudeFt = 15000.0;
    };

    static vsg::ref_ptr<OsmTileManager> create(vsg::ref_ptr<vsg::Options> options, Config cfg)
    {
        return vsg::ref_ptr<OsmTileManager>(new OsmTileManager(std::move(options), std::move(cfg)));
    }
    static vsg::ref_ptr<OsmTileManager> create(vsg::ref_ptr<vsg::Options> options)
    {
        return create(std::move(options), Config{});
    }

    OsmTileManager(vsg::ref_ptr<vsg::Options> options, Config cfg);

    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }
    bool active() const { return active_; }
    int currentZoom() const { return currentZoom_; }
    double currentLatDeg() const { return currentLatDeg_; }
    double currentLonDeg() const { return currentLonDeg_; }
    double currentAltitudeFt() const { return currentAltitudeFt_; }
    size_t cachedTileCount() const { return tileCache_.size(); }
    size_t visibleTileCount() const { return visibleTiles_.size(); }
    std::vector<std::pair<TileKey, vsg::ref_ptr<vsg::Data>>> loadedVisibleTiles() const;

    void update(const vsg::dvec3& eyeWorld, const vsg::dmat4& globeRotation, double equatorialRadiusFt, double polarRadiusFt);

private:
    bool computeSubCameraGeo(const vsg::dvec3& eyeWorld, const vsg::dmat4& globeRotation, double equatorialRadiusFt, double polarRadiusFt,
                             double& outLatDeg, double& outLonDeg, double& outAltitudeFt) const;
    int chooseZoomForAltitude(double altitudeFt) const;
    void requestVisibleTiles(double latDeg, double lonDeg, int zoom);
    void fetchAndDecodeBudgeted();

    vsg::ref_ptr<vsg::Options> options_;
    Config cfg_{};
    bool enabled_ = false;
    bool active_ = false;
    int currentZoom_ = 0;
    double currentLatDeg_ = 0.0;
    double currentLonDeg_ = 0.0;
    double currentAltitudeFt_ = 0.0;
    std::set<TileKey> visibleTiles_{};
    std::map<TileKey, TileEntry> tileCache_{};
};

} // namespace vkglobe
