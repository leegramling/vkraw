#include "vkvsg/VsgVisualizer.h"
#include "vkvsg/UIObject.h"

#include <vsg/all.h>
#include <vsgImGui/RenderImGui.h>
#include <vsgImGui/SendEventsToImGui.h>
#include <vsgImGui/imgui.h>
#ifdef VKVSG_HAS_VSGXCHANGE
#include <vsgXchange/all.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
constexpr double kMetersToFeet = 3.280839895013123;
constexpr double kWgs84EquatorialRadiusMeters = 6378137.0;
constexpr double kWgs84PolarRadiusMeters = 6356752.314245;
constexpr double kWgs84EquatorialRadiusFeet = kWgs84EquatorialRadiusMeters * kMetersToFeet;
constexpr double kWgs84PolarRadiusFeet = kWgs84PolarRadiusMeters * kMetersToFeet;

struct AppState : public vsg::Inherit<vsg::Object, AppState>
{
    vkvsg::UIObject ui;
    bool wireframe = false;
    bool textureFromFile = false;
};

class CompositeInstrumentation : public vsg::Inherit<vsg::Instrumentation, CompositeInstrumentation>
{
public:
    void add(vsg::ref_ptr<vsg::Instrumentation> instrumentation)
    {
        if (instrumentation) instrumentations.push_back(std::move(instrumentation));
    }

    vsg::ref_ptr<vsg::Instrumentation> shareOrDuplicateForThreadSafety() override
    {
        auto shared = CompositeInstrumentation::create();
        shared->instrumentations.reserve(instrumentations.size());
        for (const auto& instrumentation : instrumentations)
        {
            shared->add(vsg::shareOrDuplicateForThreadSafety(instrumentation));
        }
        return shared;
    }

    void setThreadName(const std::string& name) const override
    {
        for (const auto& instrumentation : instrumentations) instrumentation->setThreadName(name);
    }

    void enterFrame(const vsg::SourceLocation* sl, uint64_t& reference, vsg::FrameStamp& frameStamp) const override
    {
        std::vector<uint64_t> nestedReferences;
        nestedReferences.reserve(instrumentations.size());
        for (const auto& instrumentation : instrumentations)
        {
            uint64_t nestedReference = 0;
            instrumentation->enterFrame(sl, nestedReference, frameStamp);
            nestedReferences.push_back(nestedReference);
        }
        reference = storeReferences(std::move(nestedReferences));
    }

    void leaveFrame(const vsg::SourceLocation* sl, uint64_t& reference, vsg::FrameStamp& frameStamp) const override
    {
        const auto nestedReferences = consumeReferences(reference);
        if (nestedReferences.size() != instrumentations.size()) return;

        for (size_t i = 0; i < instrumentations.size(); ++i)
        {
            uint64_t nestedReference = nestedReferences[i];
            instrumentations[i]->leaveFrame(sl, nestedReference, frameStamp);
        }
    }

    void enter(const vsg::SourceLocation* sl, uint64_t& reference, const vsg::Object* object = nullptr) const override
    {
        std::vector<uint64_t> nestedReferences;
        nestedReferences.reserve(instrumentations.size());
        for (const auto& instrumentation : instrumentations)
        {
            uint64_t nestedReference = 0;
            instrumentation->enter(sl, nestedReference, object);
            nestedReferences.push_back(nestedReference);
        }
        reference = storeReferences(std::move(nestedReferences));
    }

    void leave(const vsg::SourceLocation* sl, uint64_t& reference, const vsg::Object* object = nullptr) const override
    {
        const auto nestedReferences = consumeReferences(reference);
        if (nestedReferences.size() != instrumentations.size()) return;

        for (size_t i = 0; i < instrumentations.size(); ++i)
        {
            uint64_t nestedReference = nestedReferences[i];
            instrumentations[i]->leave(sl, nestedReference, object);
        }
    }

    void enterCommandBuffer(const vsg::SourceLocation* sl, uint64_t& reference, vsg::CommandBuffer& commandBuffer) const override
    {
        std::vector<uint64_t> nestedReferences;
        nestedReferences.reserve(instrumentations.size());
        for (const auto& instrumentation : instrumentations)
        {
            uint64_t nestedReference = 0;
            instrumentation->enterCommandBuffer(sl, nestedReference, commandBuffer);
            nestedReferences.push_back(nestedReference);
        }
        reference = storeReferences(std::move(nestedReferences));
    }

    void leaveCommandBuffer(const vsg::SourceLocation* sl, uint64_t& reference, vsg::CommandBuffer& commandBuffer) const override
    {
        const auto nestedReferences = consumeReferences(reference);
        if (nestedReferences.size() != instrumentations.size()) return;

        for (size_t i = 0; i < instrumentations.size(); ++i)
        {
            uint64_t nestedReference = nestedReferences[i];
            instrumentations[i]->leaveCommandBuffer(sl, nestedReference, commandBuffer);
        }
    }

    void enter(const vsg::SourceLocation* sl, uint64_t& reference, vsg::CommandBuffer& commandBuffer, const vsg::Object* object = nullptr) const override
    {
        std::vector<uint64_t> nestedReferences;
        nestedReferences.reserve(instrumentations.size());
        for (const auto& instrumentation : instrumentations)
        {
            uint64_t nestedReference = 0;
            instrumentation->enter(sl, nestedReference, commandBuffer, object);
            nestedReferences.push_back(nestedReference);
        }
        reference = storeReferences(std::move(nestedReferences));
    }

    void leave(const vsg::SourceLocation* sl, uint64_t& reference, vsg::CommandBuffer& commandBuffer, const vsg::Object* object = nullptr) const override
    {
        const auto nestedReferences = consumeReferences(reference);
        if (nestedReferences.size() != instrumentations.size()) return;

        for (size_t i = 0; i < instrumentations.size(); ++i)
        {
            uint64_t nestedReference = nestedReferences[i];
            instrumentations[i]->leave(sl, nestedReference, commandBuffer, object);
        }
    }

    void finish() const override
    {
        for (const auto& instrumentation : instrumentations) instrumentation->finish();
    }

private:
    uint64_t storeReferences(std::vector<uint64_t> nestedReferences) const
    {
        const uint64_t key = nextReference.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(referenceMutex);
        referenceMap[key] = std::move(nestedReferences);
        return key;
    }

    std::vector<uint64_t> consumeReferences(uint64_t key) const
    {
        std::lock_guard<std::mutex> lock(referenceMutex);
        auto it = referenceMap.find(key);
        if (it == referenceMap.end()) return {};

        auto nestedReferences = std::move(it->second);
        referenceMap.erase(it);
        return nestedReferences;
    }

    std::vector<vsg::ref_ptr<vsg::Instrumentation>> instrumentations;
    mutable std::atomic<uint64_t> nextReference{1};
    mutable std::mutex referenceMutex;
    mutable std::unordered_map<uint64_t, std::vector<uint64_t>> referenceMap;
};

class GlobeRenderObject : public vsg::Inherit<vsg::Group, GlobeRenderObject>
{
};

class EquatorRenderObject : public vsg::Inherit<vsg::Group, EquatorRenderObject>
{
};

class UiRenderObject : public vsg::Inherit<vsg::Group, UiRenderObject>
{
};

class EquatorLineDraw : public vsg::Inherit<vsg::VertexIndexDraw, EquatorLineDraw>
{
};

class DebugLabelBegin : public vsg::Inherit<vsg::Command, DebugLabelBegin>
{
public:
    DebugLabelBegin(std::string inLabel, vsg::vec4 inColor = vsg::vec4(1.0f, 1.0f, 1.0f, 1.0f)) :
        label(std::move(inLabel)),
        color(inColor)
    {
    }

    void record(vsg::CommandBuffer& commandBuffer) const override
    {
        auto extensions = commandBuffer.getDevice()->getInstance()->getExtensions();
        if (!extensions || !extensions->vkCmdBeginDebugUtilsLabelEXT) return;

        VkDebugUtilsLabelEXT markerInfo{};
        markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        markerInfo.pLabelName = label.c_str();
        markerInfo.color[0] = color.r;
        markerInfo.color[1] = color.g;
        markerInfo.color[2] = color.b;
        markerInfo.color[3] = color.a;

        extensions->vkCmdBeginDebugUtilsLabelEXT(commandBuffer, &markerInfo);
    }

private:
    std::string label;
    vsg::vec4 color;
};

class DebugLabelEnd : public vsg::Inherit<vsg::Command, DebugLabelEnd>
{
public:
    void record(vsg::CommandBuffer& commandBuffer) const override
    {
        auto extensions = commandBuffer.getDevice()->getInstance()->getExtensions();
        if (!extensions || !extensions->vkCmdEndDebugUtilsLabelEXT) return;
        extensions->vkCmdEndDebugUtilsLabelEXT(commandBuffer);
    }
};

bool parseJsonStringField(const std::string& text, const char* key, std::string& out)
{
    const std::regex re(std::string("\"") + key + R"__("\s*:\s*"((?:\\.|[^"])*)")__");
    std::smatch m;
    if (!std::regex_search(text, m, re)) return false;
    out = m[1].str();
    return true;
}

bool loadEarthTexturePathFromConfig(const std::string& path, std::string& earthTexturePath)
{
    std::ifstream in(path);
    if (!in) return false;

    std::stringstream buffer;
    buffer << in.rdbuf();
    return parseJsonStringField(buffer.str(), "earth_texture", earthTexturePath);
}

class GlobeInputHandler : public vsg::Inherit<vsg::Visitor, GlobeInputHandler>
{
public:
    explicit GlobeInputHandler(vsg::ref_ptr<AppState> inState) :
        state(std::move(inState))
    {
    }

    void apply(vsg::KeyPressEvent& keyPress) override
    {
        if (keyPress.keyBase == vsg::KEY_w)
        {
            wireframeToggleRequested = true;
        }
    }

    bool consumeWireframeToggleRequest()
    {
        const bool requested = wireframeToggleRequested;
        wireframeToggleRequested = false;
        return requested;
    }

private:
    vsg::ref_ptr<AppState> state;
    bool wireframeToggleRequested = false;
};

bool computeRayFromPointer(const vsg::ref_ptr<vsg::Camera>& camera, int32_t x, int32_t y, vsg::dvec3& origin, vsg::dvec3& direction)
{
    if (!camera || !camera->projectionMatrix || !camera->viewMatrix || !camera->viewportState) return false;

    const VkViewport viewport = camera->getViewport();
    if (viewport.width <= 1.0f || viewport.height <= 1.0f) return false;

    const double nx = (2.0 * (static_cast<double>(x) - viewport.x) / static_cast<double>(viewport.width)) - 1.0;
    const double ny = (2.0 * (static_cast<double>(y) - viewport.y) / static_cast<double>(viewport.height)) - 1.0;

    const vsg::dmat4 invView = camera->viewMatrix->inverse();
    const vsg::dmat4 invProj = camera->projectionMatrix->inverse();

    const vsg::dvec4 nearClip(nx, ny, 0.0, 1.0);
    const vsg::dvec4 farClip(nx, ny, 1.0, 1.0);

    vsg::dvec4 nearView = invProj * nearClip;
    vsg::dvec4 farView = invProj * farClip;
    if (std::abs(nearView.w) < 1e-12 || std::abs(farView.w) < 1e-12) return false;
    nearView /= nearView.w;
    farView /= farView.w;

    const vsg::dvec4 nearWorld4 = invView * nearView;
    const vsg::dvec4 farWorld4 = invView * farView;
    origin = vsg::dvec3(nearWorld4.x, nearWorld4.y, nearWorld4.z);
    direction = vsg::normalize(vsg::dvec3(farWorld4.x - nearWorld4.x, farWorld4.y - nearWorld4.y, farWorld4.z - nearWorld4.z));
    return true;
}

bool intersectEllipsoid(const vsg::dvec3& rayOriginWorld, const vsg::dvec3& rayDirWorld,
                        const vsg::dmat4& globeRotation, double equatorialRadius, double polarRadius,
                        vsg::dvec3& hitWorld)
{
    const vsg::dmat4 invRot = vsg::inverse(globeRotation);
    const vsg::dvec4 o4 = invRot * vsg::dvec4(rayOriginWorld.x, rayOriginWorld.y, rayOriginWorld.z, 1.0);
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

class GlobeRotateHandler : public vsg::Inherit<vsg::Visitor, GlobeRotateHandler>
{
public:
    GlobeRotateHandler(vsg::ref_ptr<vsg::Camera> inCamera,
                       vsg::ref_ptr<vsg::MatrixTransform> inGlobeTransform,
                       double inEquatorialRadius,
                       double inPolarRadius) :
        camera(std::move(inCamera)),
        globeTransform(std::move(inGlobeTransform)),
        equatorialRadius(inEquatorialRadius),
        polarRadius(inPolarRadius)
    {
    }

    void apply(vsg::ButtonPressEvent& e) override
    {
        if (e.button == 1)
        {
            dragging = true;
            lastX = e.x;
            lastY = e.y;
        }
    }

    void apply(vsg::ButtonReleaseEvent& e) override
    {
        if (e.button == 1)
        {
            dragging = false;
        }
    }

    void apply(vsg::MoveEvent& e) override
    {
        if (!dragging || !globeTransform || !camera) return;

        vsg::dvec3 rayOriginPrev, rayDirPrev, rayOriginCurr, rayDirCurr;
        if (!computeRayFromPointer(camera, lastX, lastY, rayOriginPrev, rayDirPrev) ||
            !computeRayFromPointer(camera, e.x, e.y, rayOriginCurr, rayDirCurr))
        {
            lastX = e.x;
            lastY = e.y;
            return;
        }

        vsg::dvec3 hitPrev, hitCurr;
        const vsg::dmat4 currentRotation = globeTransform->matrix;
        if (!intersectEllipsoid(rayOriginPrev, rayDirPrev, currentRotation, equatorialRadius, polarRadius, hitPrev) ||
            !intersectEllipsoid(rayOriginCurr, rayDirCurr, currentRotation, equatorialRadius, polarRadius, hitCurr))
        {
            lastX = e.x;
            lastY = e.y;
            return;
        }

        const vsg::dvec3 v0 = vsg::normalize(hitPrev);
        const vsg::dvec3 v1 = vsg::normalize(hitCurr);
        const double dotv = std::clamp(vsg::dot(v0, v1), -1.0, 1.0);
        const double angle = std::acos(dotv);
        const vsg::dvec3 axis = vsg::cross(v0, v1);
        const double axisLen = vsg::length(axis);

        if (axisLen > 1e-10 && angle > 1e-10)
        {
            const vsg::dmat4 delta = vsg::rotate(angle, axis / axisLen);
            globeTransform->matrix = delta * currentRotation;
        }

        lastX = e.x;
        lastY = e.y;
    }

    void apply(vsg::ScrollWheelEvent& e) override
    {
        if (!camera) return;
        auto lookAt = camera->viewMatrix.cast<vsg::LookAt>();
        if (!lookAt) return;

        const double zoomScale = (e.delta.y > 0.0f) ? 0.9 : 1.1;
        vsg::dvec3 eyeDir = lookAt->eye - lookAt->center;
        double distance = vsg::length(eyeDir);
        if (distance < 1.0) return;

        distance *= zoomScale;
        const double minDistance = equatorialRadius * 1.01;
        const double maxDistance = equatorialRadius * 50.0;
        distance = std::clamp(distance, minDistance, maxDistance);

        lookAt->eye = lookAt->center + vsg::normalize(eyeDir) * distance;
    }

private:
    vsg::ref_ptr<vsg::Camera> camera;
    vsg::ref_ptr<vsg::MatrixTransform> globeTransform;
    double equatorialRadius;
    double polarRadius;
    bool dragging = false;
    int32_t lastX = 0;
    int32_t lastY = 0;
};

class GlobeGui : public vsg::Inherit<vsg::Command, GlobeGui>
{
public:
    explicit GlobeGui(vsg::ref_ptr<AppState> inState) :
        state(std::move(inState))
    {
    }

    void record(vsg::CommandBuffer&) const override
    {
        state->ui.draw(state->wireframe, state->textureFromFile);
    }

private:
    vsg::ref_ptr<AppState> state;
};

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

vsg::ref_ptr<vsg::Node> createGlobeNode(const std::string& texturePath, bool wireframe, bool& loadedFromFile)
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

    const double rx = kWgs84EquatorialRadiusFeet;
    const double ry = kWgs84EquatorialRadiusFeet;
    const double rz = kWgs84PolarRadiusFeet;

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

vsg::ref_ptr<vsg::Node> createEquatorLineNode()
{
    auto stateGroup = vsg::StateGroup::create();

#ifndef VKVSG_SHADER_DIR
#define VKVSG_SHADER_DIR ""
#endif

    const std::string vertPath = std::string(VKVSG_SHADER_DIR) + "/equator_line.vert.spv";
    const std::string fragPath = std::string(VKVSG_SHADER_DIR) + "/equator_line.frag.spv";
    auto vertexShader = vsg::ShaderStage::read(VK_SHADER_STAGE_VERTEX_BIT, "main", vertPath);
    auto fragmentShader = vsg::ShaderStage::read(VK_SHADER_STAGE_FRAGMENT_BIT, "main", fragPath);
    if (!vertexShader || !fragmentShader) return {};

    vsg::VertexInputState::Bindings bindings{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX},
        VkVertexInputBindingDescription{1, sizeof(vsg::vec4), VK_VERTEX_INPUT_RATE_VERTEX}};

    vsg::VertexInputState::Attributes attributes{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0}};

    auto rasterizationState = vsg::RasterizationState::create();
    rasterizationState->cullMode = VK_CULL_MODE_NONE;

    auto depthStencilState = vsg::DepthStencilState::create();
    depthStencilState->depthTestEnable = VK_FALSE;
    depthStencilState->depthWriteEnable = VK_FALSE;

    auto dynamicState = vsg::DynamicState::create();
    dynamicState->dynamicStates = {VK_DYNAMIC_STATE_LINE_WIDTH};

    vsg::GraphicsPipelineStates pipelineStates{
        vsg::VertexInputState::create(bindings, attributes),
        vsg::InputAssemblyState::create(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_FALSE),
        rasterizationState,
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(),
        depthStencilState,
        dynamicState};

    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128}};

    auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{}, pushConstantRanges);
    auto graphicsPipeline = vsg::GraphicsPipeline::create(pipelineLayout, vsg::ShaderStages{vertexShader, fragmentShader}, pipelineStates);
    stateGroup->add(vsg::BindGraphicsPipeline::create(graphicsPipeline));

    constexpr uint32_t segmentCount = 256;
    auto vertices = vsg::vec3Array::create(segmentCount);
    auto colors = vsg::vec4Array::create(segmentCount);

    const double radius = kWgs84EquatorialRadiusFeet * 1.002;
    for (uint32_t i = 0; i < segmentCount; ++i)
    {
        const double t = (static_cast<double>(i) / static_cast<double>(segmentCount)) * (2.0 * vsg::PI);
        const float x = static_cast<float>(-std::sin(t) * radius);
        const float y = static_cast<float>(std::cos(t) * radius);
        (*vertices)[i] = vsg::vec3(x, y, 0.0f);
        (*colors)[i] = vsg::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    auto indices = vsg::ushortArray::create(segmentCount * 2);
    uint32_t write = 0;
    for (uint32_t i = 0; i < segmentCount; ++i)
    {
        const uint16_t i0 = static_cast<uint16_t>(i);
        const uint16_t i1 = static_cast<uint16_t>((i + 1) % segmentCount);
        (*indices)[write++] = i0;
        (*indices)[write++] = i1;
    }

    auto equatorDraw = EquatorLineDraw::create();
    equatorDraw->assignArrays(vsg::DataList{vertices, colors});
    equatorDraw->assignIndices(indices);
    equatorDraw->indexCount = static_cast<uint32_t>(indices->size());
    equatorDraw->instanceCount = 1;

    auto equatorCommands = vsg::Commands::create();
    equatorCommands->addChild(DebugLabelBegin::create("EquatorLineDraw", vsg::vec4(1.0f, 1.0f, 1.0f, 1.0f)));
    equatorCommands->addChild(vsg::SetLineWidth::create(3.0f));
    equatorCommands->addChild(equatorDraw);
    equatorCommands->addChild(DebugLabelEnd::create());

    stateGroup->addChild(equatorCommands);
    auto equatorRenderObject = EquatorRenderObject::create();
    equatorRenderObject->addChild(stateGroup);
    return equatorRenderObject;
}

double latestVsgGpuFrameMs(const vsg::Profiler& profiler)
{
    if (!profiler.log || profiler.log->frameIndices.empty())
    {
        return 0.0;
    }

    auto frameGpuMs = [&](uint64_t frameRef) {
        uint64_t begin = frameRef;
        uint64_t end = profiler.log->entry(begin).reference;
        if (begin > end) std::swap(begin, end);

        double totalMs = 0.0;
        for (uint64_t i = begin; i <= end; ++i)
        {
            auto& entry = profiler.log->entry(i);
            if (!entry.enter || entry.type != vsg::ProfileLog::COMMAND_BUFFER) continue;

            auto& pair = profiler.log->entry(entry.reference);
            if (entry.gpuTime == 0 || pair.gpuTime == 0) continue;

            const uint64_t minTime = std::min(entry.gpuTime, pair.gpuTime);
            const uint64_t maxTime = std::max(entry.gpuTime, pair.gpuTime);
            totalMs += static_cast<double>(maxTime - minTime) * profiler.log->timestampScaleToMilliseconds;
        }
        return totalMs;
    };

    for (auto it = profiler.log->frameIndices.rbegin(); it != profiler.log->frameIndices.rend(); ++it)
    {
        const double ms = frameGpuMs(*it);
        if (ms > 0.0) return ms;
    }
    return 0.0;
}

} // namespace

int vkvsg::VsgVisualizer::run(int argc, char** argv)
{
    try
    {
        vsg::CommandLine arguments(&argc, argv);

        auto windowTraits = vsg::WindowTraits::create(arguments);
        windowTraits->windowTitle = "vkvsg";
        if (std::find(windowTraits->instanceExtensionNames.begin(), windowTraits->instanceExtensionNames.end(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == windowTraits->instanceExtensionNames.end())
        {
            windowTraits->instanceExtensionNames.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        windowTraits->width = 1280;
        windowTraits->height = 720;
        windowTraits->swapchainPreferences.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        windowTraits->debugUtils = true;

        float runDurationSeconds = 0.0f;
        std::string earthTexturePath;
        std::string configPath = "vkvsg.json";
        arguments.read("--seconds", runDurationSeconds);
        arguments.read("--duration", runDurationSeconds);
        while (arguments.read("--config", configPath)) {}
        loadEarthTexturePathFromConfig(configPath, earthTexturePath);
        while (arguments.read("--earth-texture", earthTexturePath)) {}

        if (arguments.errors()) return arguments.writeErrorMessages(std::cerr);

        auto viewer = vsg::Viewer::create();
        auto window = vsg::Window::create(windowTraits);
        if (!window)
        {
            std::cerr << "Could not create VSG window." << std::endl;
            return 1;
        }
        viewer->addWindow(window);

        bool hasDebugUtilsLabels = false;
        if (auto device = window->getOrCreateDevice())
        {
            if (auto instance = device->getInstance())
            {
                if (auto extensions = instance->getExtensions())
                {
                    hasDebugUtilsLabels = (extensions->vkCmdBeginDebugUtilsLabelEXT != nullptr) &&
                                          (extensions->vkCmdEndDebugUtilsLabelEXT != nullptr);
                }
            }
        }

        auto scene = vsg::Group::create();
        auto globeTransform = vsg::MatrixTransform::create();
        scene->addChild(globeTransform);

        auto ellipsoidModel = vsg::EllipsoidModel::create(kWgs84EquatorialRadiusFeet, kWgs84PolarRadiusFeet);
        scene->setObject("EllipsoidModel", ellipsoidModel);

        auto appState = AppState::create();
        bool loadedFromFile = false;
        auto globeNode = createGlobeNode(earthTexturePath, appState->wireframe, loadedFromFile);
        appState->textureFromFile = loadedFromFile;
        if (!globeNode)
        {
            std::cerr << "Failed to create globe scene node." << std::endl;
            return 1;
        }
        globeTransform->addChild(globeNode);

        auto equatorNode = createEquatorLineNode();
        if (!equatorNode)
        {
            std::cerr << "[vkvsg] Failed to create equator line node; continuing without equator." << std::endl;
        }
        else
        {
            globeTransform->addChild(equatorNode);
        }

        const double radius = kWgs84EquatorialRadiusFeet;
        const double aspect = static_cast<double>(window->extent2D().width) / static_cast<double>(window->extent2D().height);

        auto lookAt = vsg::LookAt::create(
            vsg::dvec3(0.0, -radius * 2.7, radius * 0.7),
            vsg::dvec3(0.0, 0.0, 0.0),
            vsg::dvec3(0.0, 0.0, 1.0));

        auto perspective = vsg::EllipsoidPerspective::create(lookAt, ellipsoidModel, 35.0, aspect, 0.0005, 0.0);
        auto camera = vsg::Camera::create(perspective, lookAt, vsg::ViewportState::create(window->extent2D()));

        auto commandGraph = vsg::CommandGraph::create(window);
        auto renderGraph = vsg::RenderGraph::create(window);
        commandGraph->addChild(renderGraph);

        auto globeRenderObject = GlobeRenderObject::create();
        globeRenderObject->addChild(scene);

        auto view = vsg::View::create(camera);
        view->addChild(globeRenderObject);
        renderGraph->addChild(view);

        uint64_t frameCount = 0;
        float runSeconds = 0.0f;
        float cpuFrameMs = 0.0f;

        std::cout << "[START] vkvsg globe=true"
                  << " radius_ft=" << kWgs84EquatorialRadiusFeet
                  << " wireframe=" << (appState->wireframe ? "on" : "off")
                  << " texture=" << (appState->textureFromFile ? "file" : "procedural")
                  << " present_mode=" << appState->ui.presentModeName
                  << " gpu_profiler=on"
                  << std::endl;

        auto renderImGui = vsgImGui::RenderImGui::create(window, GlobeGui::create(appState));
        auto uiRenderObject = UiRenderObject::create();
        uiRenderObject->addChild(renderImGui);
        renderGraph->addChild(uiRenderObject);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.MouseDrawCursor = true;

        auto inputHandler = GlobeInputHandler::create(appState);
        auto globeRotateHandler = GlobeRotateHandler::create(camera, globeTransform, kWgs84EquatorialRadiusFeet, kWgs84PolarRadiusFeet);

        auto profilerSettings = vsg::Profiler::Settings::create();
        profilerSettings->cpu_instrumentation_level = 0;
        profilerSettings->gpu_instrumentation_level = 1;
        auto profiler = vsg::Profiler::create(profilerSettings);
        if (hasDebugUtilsLabels)
        {
            auto gpuAnnotation = vsg::GpuAnnotation::create();
            gpuAnnotation->labelType = vsg::GpuAnnotation::Object_className;

            auto instrumentation = CompositeInstrumentation::create();
            instrumentation->add(profiler);
            instrumentation->add(gpuAnnotation);
            viewer->assignInstrumentation(instrumentation);
        }
        else
        {
            std::cerr << "[vkvsg] VK_EXT_debug_utils labels unavailable; using profiler instrumentation only." << std::endl;
            viewer->assignInstrumentation(profiler);
        }

        viewer->addEventHandler(vsgImGui::SendEventsToImGui::create());
        viewer->addEventHandler(vsg::CloseHandler::create(viewer));
        viewer->addEventHandler(globeRotateHandler);
        viewer->addEventHandler(inputHandler);

        viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});
        viewer->compile();

        const auto start = std::chrono::steady_clock::now();
        auto last = start;

        while (viewer->advanceToNextFrame())
        {
            const auto now = std::chrono::steady_clock::now();
            const float delta = std::chrono::duration<float>(now - last).count();
            const float elapsed = std::chrono::duration<float>(now - start).count();
            last = now;
            ++frameCount;
            runSeconds = elapsed;
            cpuFrameMs = 1000.0f * delta;

            if (runDurationSeconds > 0.0f && runSeconds >= runDurationSeconds)
            {
                break;
            }

            viewer->handleEvents();

            if (inputHandler->consumeWireframeToggleRequest())
            {
                appState->wireframe = !appState->wireframe;
                globeTransform->children.clear();
                bool loadedTexture = false;
                auto rebuilt = createGlobeNode(earthTexturePath, appState->wireframe, loadedTexture);
                appState->textureFromFile = loadedTexture;
                if (!rebuilt)
                {
                    std::cerr << "Failed to rebuild globe node." << std::endl;
                    return 1;
                }
                globeTransform->addChild(rebuilt);
                auto rebuiltEquator = createEquatorLineNode();
                if (rebuiltEquator) globeTransform->addChild(rebuiltEquator);
            }

            appState->ui.deltaTimeMs = 1000.0f * delta;
            appState->ui.fps = (delta > 0.0f) ? (1.0f / delta) : 0.0f;
            appState->ui.gpuFrameMs = static_cast<float>(latestVsgGpuFrameMs(*profiler));

            viewer->update();
            viewer->recordAndSubmit();
            viewer->present();
        }

        profiler->finish();
        appState->ui.gpuFrameMs = static_cast<float>(latestVsgGpuFrameMs(*profiler));

        std::cout << "[EXIT] vkvsg status=OK code=0"
                  << " frames=" << frameCount
                  << " seconds=" << runSeconds
                  << " wireframe=" << (appState->wireframe ? "on" : "off")
                  << " fps=" << appState->ui.fps
                  << " cpu_ms=" << cpuFrameMs
                  << " gpu_ms=" << appState->ui.gpuFrameMs
                  << " texture=" << (appState->textureFromFile ? "file" : "procedural")
                  << " present_mode=" << appState->ui.presentModeName
                  << std::endl;
    }
    catch (const vsg::Exception& e)
    {
        std::cout << "[EXIT] vkvsg status=FAIL code=1 reason=\"" << e.message << "\"" << std::endl;
        std::cerr << "[vsg::Exception] " << e.message << std::endl;
        return 1;
    }
    catch (const std::exception& e)
    {
        std::cout << "[EXIT] vkvsg status=FAIL code=1 reason=\"" << e.what() << "\"" << std::endl;
        std::cerr << "[Exception] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
