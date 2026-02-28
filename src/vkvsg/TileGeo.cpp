#include "vkvsg/TileGeo.h"

#include <vsg/all.h>
#ifdef VKVSG_HAS_VSGXCHANGE
#include <vsgXchange/all.h>
#endif

#include <cmath>
#include <iostream>
#include <string>

namespace
{

vsg::ref_ptr<vsg::Data> createProceduralEarthTexture()
{
    const uint32_t width = 2048;
    const uint32_t height = 1024;
    auto tex = vsg::ubvec4Array2D::create(width, height, vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM});

    for (uint32_t y = 0; y < height; ++y)
    {
        const double v = static_cast<double>(y) / static_cast<double>(height - 1);
        const double lat = (0.5 - v) * vsg::PI;
        const double polar = std::pow(std::abs(std::sin(lat)), 6.0);

        for (uint32_t x = 0; x < width; ++x)
        {
            const double u = static_cast<double>(x) / static_cast<double>(width - 1);
            const double lon = (u * 2.0 - 1.0) * vsg::PI;

            const double continent = 0.5 + 0.5 * std::sin(5.0 * lon) * std::cos(3.0 * lat);
            const bool isLand = continent > 0.62 || (std::abs(lat) > vsg::radians(52.0) && continent > 0.48);

            vsg::ubvec4 color;
            if (polar > 0.82)
            {
                color = vsg::ubvec4(236, 244, 252, 255);
            }
            else if (isLand)
            {
                const uint8_t g = static_cast<uint8_t>(90 + 80 * (1.0 - polar));
                color = vsg::ubvec4(45, g, 52, 255);
            }
            else
            {
                const uint8_t b = static_cast<uint8_t>(130 + 70 * (1.0 - polar));
                color = vsg::ubvec4(20, 65, b, 255);
            }

            tex->set(x, y, color);
        }
    }

    tex->dirty();
    return tex;
}

vsg::ref_ptr<vsg::Data> loadEarthTexture(const std::string& texturePath, bool& loadedFromFile)
{
    loadedFromFile = false;

    if (!texturePath.empty())
    {
        auto options = vsg::Options::create();
#ifdef VKVSG_HAS_VSGXCHANGE
        options->add(vsgXchange::all::create());
#endif
        auto data = vsg::read_cast<vsg::Data>(texturePath, options);
        if (data)
        {
            loadedFromFile = true;
            return data;
        }

        std::cerr << "Failed to load earth texture at '" << texturePath
                  << "', using procedural fallback texture." << std::endl;
    }

    return createProceduralEarthTexture();
}

} // namespace

vsg::ref_ptr<vsg::Node> vkvsg::createGlobeNode(const std::string& texturePath,
                                               bool wireframe,
                                               double equatorialRadiusFeet,
                                               double polarRadiusFeet,
                                               bool& loadedFromFile)
{
    auto builder = vsg::Builder::create();

    vsg::StateInfo stateInfo;
    stateInfo.wireframe = wireframe;
    stateInfo.two_sided = false;
    stateInfo.lighting = false;
    stateInfo.image = loadEarthTexture(texturePath, loadedFromFile);
    const bool topLeftOrigin = stateInfo.image && stateInfo.image->properties.origin == vsg::TOP_LEFT;

    constexpr uint32_t numColumns = 256;
    constexpr uint32_t numRows = 128;
    const uint32_t numVertices = numColumns * numRows;

    auto vertices = vsg::vec3Array::create(numVertices);
    auto normals = vsg::vec3Array::create(numVertices);
    auto texcoords = vsg::vec2Array::create(numVertices);

    const double rx = equatorialRadiusFeet;
    const double ry = equatorialRadiusFeet;
    const double rz = polarRadiusFeet;

    for (uint32_t r = 0; r < numRows; ++r)
    {
        const double v = static_cast<double>(r) / static_cast<double>(numRows - 1);
        const double beta = (v - 0.5) * vsg::PI;
        const double cosBeta = std::cos(beta);
        const double sinBeta = std::sin(beta);

        for (uint32_t c = 0; c < numColumns; ++c)
        {
            const double u = static_cast<double>(c) / static_cast<double>(numColumns - 1);
            const double alpha = u * 2.0 * vsg::PI;
            const double sinAlpha = std::sin(alpha);
            const double cosAlpha = std::cos(alpha);

            const uint32_t idx = r * numColumns + c;
            const double x = -sinAlpha * cosBeta * rx;
            const double y = cosAlpha * cosBeta * ry;
            const double z = sinBeta * rz;

            (*vertices)[idx] = vsg::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));

            vsg::dvec3 n(
                x / (rx * rx),
                y / (ry * ry),
                z / (rz * rz));
            n = vsg::normalize(n);
            (*normals)[idx] = vsg::vec3(static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z));
            const double ty = topLeftOrigin ? (1.0 - v) : v;
            (*texcoords)[idx] = vsg::vec2(static_cast<float>(u), static_cast<float>(ty));
        }
    }

    vsg::ref_ptr<vsg::ushortArray> indices;
    if (wireframe)
    {
        const uint32_t numLineIndices = (numColumns - 1) * (numRows - 1) * 8;
        indices = vsg::ushortArray::create(numLineIndices);
        uint32_t write = 0;
        for (uint32_t r = 0; r < numRows - 1; ++r)
        {
            for (uint32_t c = 0; c < numColumns - 1; ++c)
            {
                const uint16_t i00 = static_cast<uint16_t>(r * numColumns + c);
                const uint16_t i01 = static_cast<uint16_t>(i00 + 1);
                const uint16_t i10 = static_cast<uint16_t>(i00 + numColumns);
                const uint16_t i11 = static_cast<uint16_t>(i10 + 1);

                (*indices)[write++] = i00;
                (*indices)[write++] = i01;
                (*indices)[write++] = i00;
                (*indices)[write++] = i10;
                (*indices)[write++] = i01;
                (*indices)[write++] = i11;
                (*indices)[write++] = i10;
                (*indices)[write++] = i11;
            }
        }
    }
    else
    {
        const uint32_t numTriIndices = (numColumns - 1) * (numRows - 1) * 6;
        indices = vsg::ushortArray::create(numTriIndices);
        uint32_t write = 0;
        for (uint32_t r = 0; r < numRows - 1; ++r)
        {
            for (uint32_t c = 0; c < numColumns - 1; ++c)
            {
                const uint16_t i00 = static_cast<uint16_t>(r * numColumns + c);
                const uint16_t i01 = static_cast<uint16_t>(i00 + 1);
                const uint16_t i10 = static_cast<uint16_t>(i00 + numColumns);
                const uint16_t i11 = static_cast<uint16_t>(i10 + 1);

                (*indices)[write++] = i00;
                (*indices)[write++] = i01;
                (*indices)[write++] = i10;
                (*indices)[write++] = i10;
                (*indices)[write++] = i01;
                (*indices)[write++] = i11;
            }
        }
    }

    auto vid = vsg::VertexIndexDraw::create();
    auto colors = vsg::vec4Array::create(1);
    (*colors)[0] = vsg::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    vid->assignArrays(vsg::DataList{vertices, normals, texcoords, colors});
    vid->assignIndices(indices);
    vid->indexCount = static_cast<uint32_t>(indices->size());
    vid->instanceCount = 1;

    auto stateGroup = builder->createStateGroup(stateInfo);
    if (!stateGroup) return {};
    stateGroup->addChild(vid);
    return stateGroup;
}

