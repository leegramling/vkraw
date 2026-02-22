#include "vkglobe/VsgVisualizer.h"
#include "vkglobe/OsmTileManager.h"
#include "vkglobe/GlobeTileLayer.h"
#include "vkglobe/UIObject.h"

#include <vsg/all.h>
#include <vsgImGui/RenderImGui.h>
#include <vsgImGui/SendEventsToImGui.h>
#include <vsgImGui/imgui.h>
#ifdef VKVSG_HAS_VSGXCHANGE
#include <vsgXchange/all.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

namespace
{
constexpr double kMetersToFeet = 3.280839895013123;
constexpr double kWgs84EquatorialRadiusMeters = 6378137.0;
constexpr double kWgs84PolarRadiusMeters = 6356752.314245;
constexpr double kWgs84EquatorialRadiusFeet = kWgs84EquatorialRadiusMeters * kMetersToFeet;
constexpr double kWgs84PolarRadiusFeet = kWgs84PolarRadiusMeters * kMetersToFeet;
constexpr double kStartLatDeg = 37.775115;   // vsgLayt origin lat
constexpr double kStartLonDeg = -122.419241; // vsgLayt origin lon

vsg::dvec3 worldFromLatLon(double latDeg, double lonDeg)
{
    const double latRad = vsg::radians(latDeg);
    const double lonRad = vsg::radians(lonDeg);
    return vsg::dvec3(
        std::sin(lonRad) * std::cos(latRad),
        -std::cos(lonRad) * std::cos(latRad),
        std::sin(latRad));
}

struct AppState : public vsg::Inherit<vsg::Object, AppState>
{
    vkglobe::UIObject ui;
    bool wireframe = false;
    bool textureFromFile = false;
    bool exitRequested = false;
    bool osmEnabled = false;
    bool osmActive = false;
    int osmZoom = 0;
    double osmAltitudeFt = 0.0;
    size_t osmVisibleTiles = 0;
    size_t osmCachedTiles = 0;
};

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
        const double minDistance = equatorialRadius + 100.0;
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
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Exit")) state->exitRequested = true;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        state->ui.draw(state->wireframe, state->textureFromFile, state->osmEnabled, state->osmActive, state->osmZoom,
                       state->osmAltitudeFt, state->osmVisibleTiles, state->osmCachedTiles);
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

int vkglobe::VsgVisualizer::run(int argc, char** argv)
{
    try
    {
        vsg::CommandLine arguments(&argc, argv);

        auto windowTraits = vsg::WindowTraits::create(arguments);
        windowTraits->windowTitle = "vkglobe";
        windowTraits->width = 1280;
        windowTraits->height = 720;
        windowTraits->swapchainPreferences.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;

        float runDurationSeconds = 0.0f;
        std::string earthTexturePath;
        bool osmEnabled = false;
        std::string osmCachePath = "cache/osm";
        double osmEnableAltFt = 10000.0;
        double osmDisableAltFt = 15000.0;
        int osmMaxZoom = 19;
        arguments.read("--seconds", runDurationSeconds);
        arguments.read("--duration", runDurationSeconds);
        while (arguments.read("--earth-texture", earthTexturePath)) {}
        while (arguments.read("--osm")) osmEnabled = true;
        while (arguments.read("--osm-cache", osmCachePath)) {}
        while (arguments.read("--osm-enable-alt-ft", osmEnableAltFt)) {}
        while (arguments.read("--osm-disable-alt-ft", osmDisableAltFt)) {}
        while (arguments.read("--osm-max-zoom", osmMaxZoom)) {}

        if (arguments.errors()) return arguments.writeErrorMessages(std::cerr);

        auto viewer = vsg::Viewer::create();
        auto window = vsg::Window::create(windowTraits);
        if (!window)
        {
            std::cerr << "Could not create VSG window." << std::endl;
            return 1;
        }
        viewer->addWindow(window);

        auto scene = vsg::Group::create();
        auto globeTransform = vsg::MatrixTransform::create();
        scene->addChild(globeTransform);

        auto ellipsoidModel = vsg::EllipsoidModel::create(kWgs84EquatorialRadiusFeet, kWgs84PolarRadiusFeet);
        scene->setObject("EllipsoidModel", ellipsoidModel);

        auto appState = AppState::create();
        appState->osmEnabled = osmEnabled;
        bool loadedFromFile = false;
        auto globeNode = createGlobeNode(earthTexturePath, appState->wireframe, loadedFromFile);
        appState->textureFromFile = loadedFromFile;
        if (!globeNode)
        {
            std::cerr << "Failed to create globe scene node." << std::endl;
            return 1;
        }
        globeTransform->addChild(globeNode);
        auto osmTileLayer = GlobeTileLayer::create(kWgs84EquatorialRadiusFeet * 1.0005, kWgs84PolarRadiusFeet * 1.0005);
        globeTransform->addChild(osmTileLayer->root());

        const double aspect = static_cast<double>(window->extent2D().width) / static_cast<double>(window->extent2D().height);
        const double startAltitudeFt = 5000.0;
        const vsg::dvec3 startDir = vsg::normalize(worldFromLatLon(kStartLatDeg, kStartLonDeg));
        const vsg::dvec3 startSurface(
            std::sin(vsg::radians(kStartLonDeg)) * std::cos(vsg::radians(kStartLatDeg)) * kWgs84EquatorialRadiusFeet,
            -std::cos(vsg::radians(kStartLonDeg)) * std::cos(vsg::radians(kStartLatDeg)) * kWgs84EquatorialRadiusFeet,
            std::sin(vsg::radians(kStartLatDeg)) * kWgs84PolarRadiusFeet);
        const double startSurfaceRadius = vsg::length(startSurface);
        const vsg::dvec3 startEye = startDir * (startSurfaceRadius + startAltitudeFt);
        const vsg::dvec3 startUp = vsg::normalize(vsg::cross(vsg::cross(startDir, vsg::dvec3(0.0, 0.0, 1.0)), startDir));

        auto lookAt = vsg::LookAt::create(
            startEye,
            vsg::dvec3(0.0, 0.0, 0.0),
            startUp);

        auto perspective = vsg::EllipsoidPerspective::create(lookAt, ellipsoidModel, 35.0, aspect, 0.0005, 0.0);
        auto camera = vsg::Camera::create(perspective, lookAt, vsg::ViewportState::create(window->extent2D()));

        auto runtimeOptions = vsg::Options::create();
#ifdef VKVSG_HAS_VSGXCHANGE
        runtimeOptions->add(vsgXchange::all::create());
#endif
        OsmTileManager::Config osmConfig{};
        osmConfig.cacheRoot = osmCachePath;
        osmConfig.enableAltitudeFt = osmEnableAltFt;
        osmConfig.disableAltitudeFt = osmDisableAltFt;
        osmConfig.maxZoom = std::clamp(osmMaxZoom, osmConfig.minZoom, 22);
        auto osmTiles = OsmTileManager::create(runtimeOptions, osmConfig);
        osmTiles->setEnabled(osmEnabled);

        auto commandGraph = vsg::CommandGraph::create(window);
        auto renderGraph = vsg::RenderGraph::create(window);
        commandGraph->addChild(renderGraph);

        auto view = vsg::View::create(camera);
        view->addChild(scene);
        renderGraph->addChild(view);

        uint64_t frameCount = 0;
        float runSeconds = 0.0f;
        float cpuFrameMs = 0.0f;

        std::cout << "[START] vkglobe globe=true"
                  << " radius_ft=" << kWgs84EquatorialRadiusFeet
                  << " wireframe=" << (appState->wireframe ? "on" : "off")
                  << " texture=" << (appState->textureFromFile ? "file" : "procedural")
                  << " osm=" << (osmEnabled ? "on" : "off")
                  << " osm_cache=" << osmCachePath
                  << " osm_enable_alt_ft=" << osmEnableAltFt
                  << " osm_disable_alt_ft=" << osmDisableAltFt
                  << " osm_max_zoom=" << osmConfig.maxZoom
                  << " present_mode=" << appState->ui.presentModeName
                  << " gpu_profiler=on"
                  << std::endl;

        auto renderImGui = vsgImGui::RenderImGui::create(window, GlobeGui::create(appState));
        renderGraph->addChild(renderImGui);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.MouseDrawCursor = true;
        ImGui::GetStyle().ScaleAllSizes(1.5f);
        io.FontGlobalScale = 1.5f;

        auto inputHandler = GlobeInputHandler::create(appState);
        auto globeRotateHandler = GlobeRotateHandler::create(camera, globeTransform, kWgs84EquatorialRadiusFeet, kWgs84PolarRadiusFeet);

        auto profilerSettings = vsg::Profiler::Settings::create();
        profilerSettings->cpu_instrumentation_level = 0;
        profilerSettings->gpu_instrumentation_level = 1;
        auto profiler = vsg::Profiler::create(profilerSettings);
        viewer->assignInstrumentation(profiler);

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

            if (appState->exitRequested) break;

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
                globeTransform->addChild(osmTileLayer->root());
            }

            appState->ui.deltaTimeMs = 1000.0f * delta;
            appState->ui.fps = (delta > 0.0f) ? (1.0f / delta) : 0.0f;
            appState->ui.gpuFrameMs = static_cast<float>(latestVsgGpuFrameMs(*profiler));

            if (osmTiles->enabled())
            {
                osmTiles->update(lookAt->eye, globeTransform->matrix, kWgs84EquatorialRadiusFeet, kWgs84PolarRadiusFeet);
                const bool tilesChanged = osmTileLayer->syncFromTiles(osmTiles->loadedVisibleTiles());
                if (tilesChanged)
                {
                    viewer->compile();
                }
                if ((frameCount % 120) == 0)
                {
                    std::cout << "[OSM] active=" << (osmTiles->active() ? "yes" : "no")
                              << " zoom=" << osmTiles->currentZoom()
                              << " lat=" << osmTiles->currentLatDeg()
                              << " lon=" << osmTiles->currentLonDeg()
                              << " alt_ft=" << osmTiles->currentAltitudeFt()
                              << " visible_tiles=" << osmTiles->visibleTileCount()
                              << " cached_tiles=" << osmTiles->cachedTileCount()
                              << std::endl;
                }
            }
            appState->osmEnabled = osmTiles->enabled();
            appState->osmActive = osmTiles->active();
            appState->osmZoom = osmTiles->currentZoom();
            appState->osmAltitudeFt = osmTiles->currentAltitudeFt();
            appState->osmVisibleTiles = osmTiles->visibleTileCount();
            appState->osmCachedTiles = osmTiles->cachedTileCount();

            viewer->update();
            viewer->recordAndSubmit();
            viewer->present();
        }

        profiler->finish();
        appState->ui.gpuFrameMs = static_cast<float>(latestVsgGpuFrameMs(*profiler));

        std::cout << "[EXIT] vkglobe status=OK code=0"
                  << " frames=" << frameCount
                  << " seconds=" << runSeconds
                  << " wireframe=" << (appState->wireframe ? "on" : "off")
                  << " fps=" << appState->ui.fps
                  << " cpu_ms=" << cpuFrameMs
                  << " gpu_ms=" << appState->ui.gpuFrameMs
                  << " texture=" << (appState->textureFromFile ? "file" : "procedural")
                  << " osm=" << (osmTiles->enabled() ? "on" : "off")
                  << " osm_active=" << (osmTiles->active() ? "yes" : "no")
                  << " osm_zoom=" << osmTiles->currentZoom()
                  << " osm_cached_tiles=" << osmTiles->cachedTileCount()
                  << " present_mode=" << appState->ui.presentModeName
                  << std::endl;
    }
    catch (const vsg::Exception& e)
    {
        std::cout << "[EXIT] vkglobe status=FAIL code=1 reason=\"" << e.message << "\"" << std::endl;
        std::cerr << "[vsg::Exception] " << e.message << std::endl;
        return 1;
    }
    catch (const std::exception& e)
    {
        std::cout << "[EXIT] vkglobe status=FAIL code=1 reason=\"" << e.what() << "\"" << std::endl;
        std::cerr << "[Exception] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
