#pragma once

#include "vkglobe/OsmTileManager.h"

#include <vsg/core/Inherit.h>
#include <vsg/core/Object.h>
#include <vsg/nodes/Group.h>

#include <map>
#include <set>

namespace vkglobe {

class GlobeTileLayer : public vsg::Inherit<vsg::Object, GlobeTileLayer>
{
public:
    static vsg::ref_ptr<GlobeTileLayer> create(double equatorialRadiusFt, double polarRadiusFt)
    {
        return vsg::ref_ptr<GlobeTileLayer>(new GlobeTileLayer(equatorialRadiusFt, polarRadiusFt));
    }

    GlobeTileLayer(double equatorialRadiusFt, double polarRadiusFt);

    vsg::ref_ptr<vsg::Group> root() const { return root_; }
    bool syncFromTiles(const std::vector<std::pair<TileKey, vsg::ref_ptr<vsg::Data>>>& loadedTiles);

private:
    vsg::ref_ptr<vsg::Node> buildTileNode(const TileKey& key, vsg::ref_ptr<vsg::Data> image) const;

    double equatorialRadiusFt_ = 0.0;
    double polarRadiusFt_ = 0.0;
    vsg::ref_ptr<vsg::Group> root_;
    std::map<TileKey, vsg::ref_ptr<vsg::Node>> activeNodes_;
};

} // namespace vkglobe

