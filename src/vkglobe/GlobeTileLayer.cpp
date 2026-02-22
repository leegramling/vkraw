#include "vkglobe/GlobeTileLayer.h"

#include <vsg/all.h>

#include <algorithm>
#include <cmath>

namespace vkglobe {

namespace {
constexpr double kPi = 3.14159265358979323846;

double tileXToLonDeg(int x, int z)
{
    const double n = static_cast<double>(1 << z);
    return (static_cast<double>(x) / n) * 360.0 - 180.0;
}

double tileYToLatDeg(int y, int z)
{
    const double n = static_cast<double>(1 << z);
    const double t = kPi * (1.0 - 2.0 * static_cast<double>(y) / n);
    return std::atan(std::sinh(t)) * (180.0 / kPi);
}

} // namespace

GlobeTileLayer::GlobeTileLayer(double equatorialRadiusFt, double polarRadiusFt, vsg::ref_ptr<vsg::StateGroup> stateTemplate, vsg::ref_ptr<vsg::Data> fallbackImage) :
    equatorialRadiusFt_(equatorialRadiusFt),
    polarRadiusFt_(polarRadiusFt),
    stateTemplate_(std::move(stateTemplate)),
    fallbackImage_(std::move(fallbackImage)),
    root_(vsg::Group::create())
{
}

bool GlobeTileLayer::syncFromTileWindow(const std::vector<TileSample>& tileWindow)
{
    bool changed = false;
    for (const TileSample& sample : tileWindow)
    {
        auto& slot = slots_[{sample.ox, sample.oy}];
        const bool keyChanged = !slot.hasKey || slot.key.z != sample.key.z || slot.key.x != sample.key.x || slot.key.y != sample.key.y;
        const bool loadChanged = (slot.loaded != sample.loaded);
        if (!keyChanged && !loadChanged) continue;

        if (slot.node)
        {
            auto it = std::find(root_->children.begin(), root_->children.end(), slot.node);
            if (it != root_->children.end()) root_->children.erase(it);
        }

        auto image = (sample.loaded && sample.image) ? sample.image : fallbackImage_;
        slot.node = buildTileNode(sample.key, image);
        if (slot.node) root_->addChild(slot.node);
        slot.key = sample.key;
        slot.hasKey = true;
        slot.loaded = sample.loaded;
        changed = true;
    }
    return changed;
}

vsg::ref_ptr<vsg::Node> GlobeTileLayer::buildTileNode(const TileKey& key, vsg::ref_ptr<vsg::Data> image) const
{
    if (!stateTemplate_) return {};
    if (!image) image = fallbackImage_;

    constexpr uint32_t cols = 24;
    constexpr uint32_t rows = 24;
    const uint32_t numVertices = cols * rows;
    auto vertices = vsg::vec3Array::create(numVertices);
    auto normals = vsg::vec3Array::create(numVertices);
    auto texcoords = vsg::vec2Array::create(numVertices);

    const double lonLeft = tileXToLonDeg(key.x, key.z);
    const double lonRight = tileXToLonDeg(key.x + 1, key.z);
    const double latTop = tileYToLatDeg(key.y, key.z);
    const double latBottom = tileYToLatDeg(key.y + 1, key.z);
    const bool topLeftOrigin = image && image->properties.origin == vsg::TOP_LEFT;

    for (uint32_t r = 0; r < rows; ++r)
    {
        const double v = static_cast<double>(r) / static_cast<double>(rows - 1);
        const double latDeg = latTop + (latBottom - latTop) * v;
        const double latRad = latDeg * (kPi / 180.0);
        const double cosLat = std::cos(latRad);
        const double sinLat = std::sin(latRad);

        for (uint32_t c = 0; c < cols; ++c)
        {
            const double u = static_cast<double>(c) / static_cast<double>(cols - 1);
            const double lonDeg = lonLeft + (lonRight - lonLeft) * u;
            const double lonRad = lonDeg * (kPi / 180.0);
            const double sinLon = std::sin(lonRad);
            const double cosLon = std::cos(lonRad);

            const uint32_t idx = r * cols + c;
            const double x = sinLon * cosLat * equatorialRadiusFt_;
            const double y = -cosLon * cosLat * equatorialRadiusFt_;
            const double z = sinLat * polarRadiusFt_;
            (*vertices)[idx] = vsg::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));

            vsg::dvec3 n(
                x / (equatorialRadiusFt_ * equatorialRadiusFt_),
                y / (equatorialRadiusFt_ * equatorialRadiusFt_),
                z / (polarRadiusFt_ * polarRadiusFt_));
            n = vsg::normalize(n);
            (*normals)[idx] = vsg::vec3(static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z));
            (*texcoords)[idx] = vsg::vec2(static_cast<float>(u), static_cast<float>(topLeftOrigin ? v : (1.0 - v)));
        }
    }

    const uint32_t numIndices = (cols - 1) * (rows - 1) * 6;
    auto indices = vsg::ushortArray::create(numIndices);
    uint32_t write = 0;
    for (uint32_t r = 0; r < rows - 1; ++r)
    {
        for (uint32_t c = 0; c < cols - 1; ++c)
        {
            const uint16_t i00 = static_cast<uint16_t>(r * cols + c);
            const uint16_t i01 = static_cast<uint16_t>(i00 + 1);
            const uint16_t i10 = static_cast<uint16_t>(i00 + cols);
            const uint16_t i11 = static_cast<uint16_t>(i10 + 1);
            // Reverse winding to match the inherited globe pipeline cull state.
            (*indices)[write++] = i00;
            (*indices)[write++] = i10;
            (*indices)[write++] = i01;
            (*indices)[write++] = i10;
            (*indices)[write++] = i11;
            (*indices)[write++] = i01;
        }
    }

    auto vid = vsg::VertexIndexDraw::create();
    auto colors = vsg::vec4Array::create(numVertices);
    // Debug tint so OSM patches are visually obvious during integration.
    const float tintR = 0.45f + 0.5f * static_cast<float>((key.x * 17) & 7) / 7.0f;
    const float tintG = 0.45f + 0.5f * static_cast<float>((key.y * 11) & 7) / 7.0f;
    const float tintB = 0.45f + 0.5f * static_cast<float>((key.z * 3) & 7) / 7.0f;
    for (uint32_t r = 0; r < rows; ++r)
    {
        for (uint32_t c = 0; c < cols; ++c)
        {
            const uint32_t i = r * cols + c;
            const bool edge = (r == 0) || (c == 0) || (r == rows - 1) || (c == cols - 1);
            if (edge)
            {
                // Strong border so the tile patch window remains obvious while OSM textures
                // are not yet bound per-tile.
                (*colors)[i] = vsg::vec4(1.0f, 0.15f, 0.15f, 1.0f);
            }
            else
            {
                (*colors)[i] = vsg::vec4(tintR, tintG, tintB, 1.0f);
            }
        }
    }
    vid->assignArrays(vsg::DataList{vertices, normals, texcoords, colors});
    vid->assignIndices(indices);
    vid->indexCount = static_cast<uint32_t>(indices->size());
    vid->instanceCount = 1;
    vsg::CopyOp copyop;
    copyop.duplicate = vsg::ref_ptr<vsg::Duplicate>(new vsg::Duplicate);
    auto tileState = stateTemplate_->clone(copyop).cast<vsg::StateGroup>();
    if (!tileState) return {};
    if (!assignTileImage(*tileState, image)) return {};
    tileState->children.clear();
    tileState->addChild(vid);
    return tileState;
}

bool GlobeTileLayer::assignTileImage(vsg::StateGroup& stateGroup, vsg::ref_ptr<vsg::Data> image) const
{
    if (!image) return false;
    for (auto& sc : stateGroup.stateCommands)
    {
        auto bds = sc.cast<vsg::BindDescriptorSet>();
        if (!bds || !bds->descriptorSet) continue;
        for (auto& descriptor : bds->descriptorSet->descriptors)
        {
            auto di = descriptor.cast<vsg::DescriptorImage>();
            if (!di || di->imageInfoList.empty() || !di->imageInfoList.front()) continue;
            auto imageInfo = di->imageInfoList.front();
            imageInfo->imageView = vsg::ImageView::create(vsg::Image::create(image));
            imageInfo->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            return true;
        }
    }
    return false;
}

} // namespace vkglobe
