#pragma once

#include "vkglobe/OsmTileManager.h"

#include <vsg/core/Inherit.h>
#include <vsg/core/Data.h>
#include <vsg/core/Object.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/state/Sampler.h>

#include <map>
#include <utility>

namespace vkglobe {

class GlobeTileLayer : public vsg::Inherit<vsg::Object, GlobeTileLayer>
{
public:
    static vsg::ref_ptr<GlobeTileLayer> create(double equatorialRadiusFt, double polarRadiusFt,
                                               vsg::ref_ptr<vsg::StateGroup> stateTemplate,
                                               vsg::ref_ptr<vsg::Data> fallbackImage)
    {
        return vsg::ref_ptr<GlobeTileLayer>(new GlobeTileLayer(equatorialRadiusFt, polarRadiusFt, std::move(stateTemplate), std::move(fallbackImage)));
    }

    GlobeTileLayer(double equatorialRadiusFt, double polarRadiusFt, vsg::ref_ptr<vsg::StateGroup> stateTemplate, vsg::ref_ptr<vsg::Data> fallbackImage);

    vsg::ref_ptr<vsg::Group> root() const { return root_; }
    bool syncFromTileWindow(const std::vector<TileSample>& tileWindow);

private:
    struct Slot
    {
        TileKey key;
        bool hasKey = false;
        bool loaded = false;
        vsg::ref_ptr<vsg::Node> node;
    };

    vsg::ref_ptr<vsg::Node> buildTileNode(const TileKey& key, vsg::ref_ptr<vsg::Data> image) const;
    bool assignTileImage(vsg::StateGroup& stateGroup, vsg::ref_ptr<vsg::Data> image) const;
    void localizeDescriptorCommands(vsg::StateGroup& stateGroup) const;

    double equatorialRadiusFt_ = 0.0;
    double polarRadiusFt_ = 0.0;
    vsg::ref_ptr<vsg::StateGroup> stateTemplate_;
    vsg::ref_ptr<vsg::Data> fallbackImage_;
    vsg::ref_ptr<vsg::Sampler> tileSampler_;
    vsg::ref_ptr<vsg::Group> root_;
    std::map<std::pair<int, int>, Slot> slots_;
};

} // namespace vkglobe
