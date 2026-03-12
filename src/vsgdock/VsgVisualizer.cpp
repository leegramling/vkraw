#include "vsgdock/VsgVisualizer.h"
#include "vsgdock/VsgInputManager.h"
#include "vsgdock/LineObject.h"
#include "vsgdock/TileGeo.h"
#include "vsgdock/UIObject.h"

#include <vsg/all.h>
#include <vsgImGui/RenderImGui.h>
#include <vsgImGui/imgui.h>

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
constexpr double kTearOffOverlapThreshold = 0.5;

struct ScopedImGuiContext
{
    explicit ScopedImGuiContext(ImGuiContext* next) :
        previous(ImGui::GetCurrentContext())
    {
        ImGui::SetCurrentContext(next);
    }

    ~ScopedImGuiContext()
    {
        ImGui::SetCurrentContext(previous);
    }

    ImGuiContext* previous = nullptr;
};

struct AppState : public vsg::Inherit<vsg::Object, AppState>
{
    vkvsg::UIObject ui;
    bool wireframe = false;
    bool textureFromFile = false;
    bool globeControlsDetached = false;
    bool dockBackRequested = false;
    bool resetMainPanelPlacement = false;
    bool suppressTearOffUntilMouseRelease = false;
    bool suppressAutoDockUntilTearOffMove = false;
    bool exitRequested = false;
    vkvsg::UIObject::PanelLayout mainPanelLayout;
    vkvsg::VsgInputManager::WindowRect tearOffInitialRect;
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

class UiRenderObject : public vsg::Inherit<vsg::Group, UiRenderObject>
{
};

class ContextAwareRenderImGui : public vsg::Inherit<vsg::Group, ContextAwareRenderImGui>
{
public:
    ContextAwareRenderImGui(ImGuiContext* inContext, vsg::ref_ptr<vsgImGui::RenderImGui> inRenderImGui) :
        context(inContext),
        renderImGui(std::move(inRenderImGui))
    {
        addChild(renderImGui);
    }

    void accept(vsg::RecordTraversal& rt) const override
    {
        ScopedImGuiContext scoped(context);
        vsg::Group::accept(rt);
    }

private:
    ImGuiContext* context = nullptr;
    vsg::ref_ptr<vsgImGui::RenderImGui> renderImGui;
};

struct UiWindowResources
{
    vsg::ref_ptr<vsg::Window> window;
    vsg::ref_ptr<vsg::CommandGraph> commandGraph;
    vsg::ref_ptr<vsg::RenderGraph> renderGraph;
    vsg::ref_ptr<UiRenderObject> uiRenderObject;
    vsg::ref_ptr<vsgImGui::RenderImGui> renderImGui;
    vsg::ref_ptr<ContextAwareRenderImGui> contextAwareRenderImGui;
    ImGuiContext* context = nullptr;
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
    GlobeRotateHandler(vsg::ref_ptr<vsg::Window> inMainWindow,
                       vsg::ref_ptr<vsg::Camera> inCamera,
                       vsg::ref_ptr<vsg::MatrixTransform> inGlobeTransform,
                       double inEquatorialRadius,
                       double inPolarRadius) :
        mainWindow(std::move(inMainWindow)),
        camera(std::move(inCamera)),
        globeTransform(std::move(inGlobeTransform)),
        equatorialRadius(inEquatorialRadius),
        polarRadius(inPolarRadius)
    {
    }

    void apply(vsg::ButtonPressEvent& e) override
    {
        if (e.handled || e.window != mainWindow.get() || e.button != 1) return;
        dragging = true;
        lastX = e.x;
        lastY = e.y;
    }

    void apply(vsg::ButtonReleaseEvent& e) override
    {
        if (e.window != mainWindow.get()) return;
        if (e.button == 1) dragging = false;
    }

    void apply(vsg::MoveEvent& e) override
    {
        if (e.handled || e.window != mainWindow.get() || !dragging || !globeTransform || !camera) return;

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
        if (e.handled || e.window != mainWindow.get() || !camera) return;
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
    vsg::ref_ptr<vsg::Window> mainWindow;
    vsg::ref_ptr<vsg::Camera> camera;
    vsg::ref_ptr<vsg::MatrixTransform> globeTransform;
    double equatorialRadius;
    double polarRadius;
    bool dragging = false;
    int32_t lastX = 0;
    int32_t lastY = 0;
};

class MainWindowGui : public vsg::Inherit<vsg::Command, MainWindowGui>
{
public:
    explicit MainWindowGui(vsg::ref_ptr<AppState> inState) :
        state(std::move(inState))
    {
    }

    void record(vsg::CommandBuffer&) const override
    {
        ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockspaceFlags);
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Exit"))
                {
                    state->exitRequested = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        if (!state->globeControlsDetached)
        {
            if (state->resetMainPanelPlacement)
            {
                ImGui::SetNextWindowPos(ImVec2(16.0f, 96.0f), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(360.0f, 170.0f), ImGuiCond_Always);
                state->resetMainPanelPlacement = false;
            }
            state->ui.drawGlobeControls(state->wireframe, state->textureFromFile, &state->mainPanelLayout);
        }
        state->ui.drawDemo();
    }

private:
    vsg::ref_ptr<AppState> state;
};

class TearOffGui : public vsg::Inherit<vsg::Command, TearOffGui>
{
public:
    explicit TearOffGui(vsg::ref_ptr<AppState> inState) :
        state(std::move(inState))
    {
    }

    void record(vsg::CommandBuffer&) const override
    {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        state->ui.drawGlobeControls(
            state->wireframe,
            state->textureFromFile,
            nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar,
            true,
            &state->dockBackRequested);
    }

private:
    vsg::ref_ptr<AppState> state;
};

double computePanelOverlapRatio(const vkvsg::UIObject::PanelLayout& panelLayout, const VkExtent2D& extent)
{
    const double panelWidth = std::max(0.0f, panelLayout.size.x);
    const double panelHeight = std::max(0.0f, panelLayout.size.y);
    const double panelArea = panelWidth * panelHeight;
    if (panelArea <= 0.0) return 1.0;

    const double left = std::max(0.0, static_cast<double>(panelLayout.pos.x));
    const double top = std::max(0.0, static_cast<double>(panelLayout.pos.y));
    const double right = std::min(static_cast<double>(extent.width), static_cast<double>(panelLayout.pos.x + panelLayout.size.x));
    const double bottom = std::min(static_cast<double>(extent.height), static_cast<double>(panelLayout.pos.y + panelLayout.size.y));

    const double overlapWidth = std::max(0.0, right - left);
    const double overlapHeight = std::max(0.0, bottom - top);
    return (overlapWidth * overlapHeight) / panelArea;
}

double computeWindowOverlapRatio(const vkvsg::VsgInputManager::WindowRect& movingRect,
                                 const vkvsg::VsgInputManager::WindowRect& targetRect)
{
    const double movingArea = static_cast<double>(movingRect.width) * static_cast<double>(movingRect.height);
    if (!movingRect.valid || !targetRect.valid || movingArea <= 0.0) return 0.0;

    const double left = std::max(static_cast<double>(movingRect.x), static_cast<double>(targetRect.x));
    const double top = std::max(static_cast<double>(movingRect.y), static_cast<double>(targetRect.y));
    const double right = std::min(static_cast<double>(movingRect.x + static_cast<int32_t>(movingRect.width)),
                                  static_cast<double>(targetRect.x + static_cast<int32_t>(targetRect.width)));
    const double bottom = std::min(static_cast<double>(movingRect.y + static_cast<int32_t>(movingRect.height)),
                                   static_cast<double>(targetRect.y + static_cast<int32_t>(targetRect.height)));

    const double overlapWidth = std::max(0.0, right - left);
    const double overlapHeight = std::max(0.0, bottom - top);
    return (overlapWidth * overlapHeight) / movingArea;
}

UiWindowResources createUiWindowResources(vsg::ref_ptr<vsg::Window> window, ImGuiContext* context, vsg::ref_ptr<vsg::Command> guiCommand, bool enableDocking)
{
    UiWindowResources resources;
    resources.window = std::move(window);
    resources.context = context;
    resources.commandGraph = vsg::CommandGraph::create(resources.window);
    resources.renderGraph = vsg::RenderGraph::create(resources.window);
    resources.commandGraph->addChild(resources.renderGraph);
    {
        ScopedImGuiContext scoped(context);
        resources.renderImGui = vsgImGui::RenderImGui::create(resources.window, guiCommand);
        ImGuiIO& io = ImGui::GetIO();
        if (enableDocking) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.MouseDrawCursor = false;
    }
    resources.uiRenderObject = UiRenderObject::create();
    resources.contextAwareRenderImGui = ContextAwareRenderImGui::create(context, resources.renderImGui);
    resources.uiRenderObject->addChild(resources.contextAwareRenderImGui);
    resources.renderGraph->addChild(resources.uiRenderObject);
    return resources;
}

void destroyUiWindowResources(UiWindowResources& resources)
{
    if (resources.context)
    {
        ScopedImGuiContext scoped(resources.context);
        resources.uiRenderObject = {};
        resources.renderGraph = {};
        resources.commandGraph = {};
        resources.contextAwareRenderImGui = {};
        resources.renderImGui = {};
    }
    resources.context = nullptr;
    resources.window = {};
}

double latestVsgGpuFrameMs(const vsg::Profiler& profiler)
{
    if (!profiler.log || profiler.log->frameIndices.empty()) return 0.0;

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
        windowTraits->windowTitle = "vsgdock";
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
        std::string configPath = "vsgdock.json";
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
        auto globeNode = vkvsg::createGlobeNode(earthTexturePath, appState->wireframe, kWgs84EquatorialRadiusFeet, kWgs84PolarRadiusFeet, loadedFromFile);
        appState->textureFromFile = loadedFromFile;
        if (!globeNode)
        {
            std::cerr << "Failed to create globe scene node." << std::endl;
            return 1;
        }
        globeTransform->addChild(globeNode);

        auto equatorNode = vkvsg::createEquatorLineNode(kWgs84EquatorialRadiusFeet);
        if (!equatorNode)
        {
            std::cerr << "[vsgdock] Failed to create equator line node; continuing without equator." << std::endl;
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

        UiWindowResources mainUi = createUiWindowResources(window, ImGui::CreateContext(), MainWindowGui::create(appState), true);
        renderGraph->addChild(mainUi.uiRenderObject);

        auto inputManager = vkvsg::VsgInputManager::create(viewer);
        inputManager->setMainWindow(window);
        inputManager->addWindow(window, mainUi.context);

        auto globeRotateHandler = GlobeRotateHandler::create(window, camera, globeTransform, kWgs84EquatorialRadiusFeet, kWgs84PolarRadiusFeet);

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
            std::cerr << "[vsgdock] VK_EXT_debug_utils labels unavailable; using profiler instrumentation only." << std::endl;
            viewer->assignInstrumentation(profiler);
        }

        viewer->addEventHandler(inputManager);
        viewer->addEventHandler(globeRotateHandler);

        viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});
        viewer->compile();

        UiWindowResources tearOffUi;

        auto createTearOffWindow = [&]() -> bool {
            if (tearOffUi.window) return true;

            auto tearOffTraits = vsg::WindowTraits::create(*window->traits());
            tearOffTraits->device = window->getOrCreateDevice();
            tearOffTraits->windowTitle = "vsgdock - Globe Controls";
            tearOffTraits->width = static_cast<uint32_t>(std::max(320.0f, appState->mainPanelLayout.size.x));
            tearOffTraits->height = static_cast<uint32_t>(std::max(180.0f, appState->mainPanelLayout.size.y));
            vkvsg::VsgInputManager::WindowRect mainRect;
            if (!inputManager->getWindowRect(window, mainRect))
            {
                mainRect.x = window->traits()->x;
                mainRect.y = window->traits()->y;
                mainRect.width = window->traits()->width;
                mainRect.height = window->traits()->height;
                mainRect.valid = true;
            }

            const float panelCenterX = appState->mainPanelLayout.pos.x + (appState->mainPanelLayout.size.x * 0.5f);
            const float mainCenterX = static_cast<float>(mainRect.width) * 0.5f;
            const bool placeLeft = panelCenterX < mainCenterX;
            const int32_t gutter = 24;

            tearOffTraits->x = placeLeft
                ? (mainRect.x - static_cast<int32_t>(tearOffTraits->width) - gutter)
                : (mainRect.x + static_cast<int32_t>(mainRect.width) + gutter);

            const float desiredTop = std::max(0.0f, appState->mainPanelLayout.pos.y);
            const float maxTop = std::max(0.0f, static_cast<float>(mainRect.height) - static_cast<float>(tearOffTraits->height));
            tearOffTraits->y = mainRect.y + static_cast<int32_t>(std::clamp(desiredTop, 0.0f, maxTop));

            auto tearOffWindow = vsg::Window::create(tearOffTraits);
            if (!tearOffWindow)
            {
                std::cerr << "[vsgdock] Failed to create tear-off window." << std::endl;
                return false;
            }

            tearOffUi = createUiWindowResources(tearOffWindow, ImGui::CreateContext(), TearOffGui::create(appState), false);
            inputManager->setTearOffWindow(tearOffWindow);
            inputManager->addWindow(tearOffWindow, tearOffUi.context);
            appState->globeControlsDetached = true;
            appState->suppressAutoDockUntilTearOffMove = true;
            inputManager->getWindowRect(tearOffWindow, appState->tearOffInitialRect);
            viewer->addRecordAndSubmitTaskAndPresentation({tearOffUi.commandGraph});
            viewer->compile();
            return true;
        };

        auto destroyTearOffWindow = [&]() {
            if (!tearOffUi.window) return;
            viewer->deviceWaitIdle();
            inputManager->removeWindow(tearOffUi.window);
            viewer->removeWindow(tearOffUi.window);
            destroyUiWindowResources(tearOffUi);
            inputManager->setTearOffWindow({});
            appState->globeControlsDetached = false;
            appState->dockBackRequested = false;
            appState->resetMainPanelPlacement = true;
            appState->suppressTearOffUntilMouseRelease = true;
            appState->suppressAutoDockUntilTearOffMove = false;
            appState->mainPanelLayout = {};
            appState->tearOffInitialRect = {};
            viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});
        };

        uint64_t frameCount = 0;
        float runSeconds = 0.0f;
        float cpuFrameMs = 0.0f;

        std::cout << "[START] vsgdock globe=true"
                  << " radius_ft=" << kWgs84EquatorialRadiusFeet
                  << " wireframe=" << (appState->wireframe ? "on" : "off")
                  << " texture=" << (appState->textureFromFile ? "file" : "procedural")
                  << " present_mode=" << appState->ui.presentModeName
                  << " gpu_profiler=on"
                  << std::endl;

        const auto start = std::chrono::steady_clock::now();
        auto last = start;
        bool pendingCreateTearOffWindow = false;
        bool pendingDestroyTearOffWindow = false;

        while (viewer->advanceToNextFrame())
        {
            const auto now = std::chrono::steady_clock::now();
            const float delta = std::chrono::duration<float>(now - last).count();
            const float elapsed = std::chrono::duration<float>(now - start).count();
            last = now;
            ++frameCount;
            runSeconds = elapsed;
            cpuFrameMs = 1000.0f * delta;

            if (runDurationSeconds > 0.0f && runSeconds >= runDurationSeconds) break;

            viewer->handleEvents();
            inputManager->processQueuedEvents();

            if (appState->exitRequested)
            {
                viewer->close();
                appState->exitRequested = false;
            }

            if (inputManager->consumeTearOffCloseRequest()) pendingDestroyTearOffWindow = true;
            if (appState->dockBackRequested) pendingDestroyTearOffWindow = true;

            if (appState->suppressTearOffUntilMouseRelease && !inputManager->leftMouseButtonDown())
            {
                appState->suppressTearOffUntilMouseRelease = false;
            }

            if (!appState->globeControlsDetached)
            {
                const double overlapRatio = computePanelOverlapRatio(appState->mainPanelLayout, window->extent2D());
                if (!appState->suppressTearOffUntilMouseRelease &&
                    appState->mainPanelLayout.moving &&
                    overlapRatio < kTearOffOverlapThreshold)
                {
                    pendingCreateTearOffWindow = true;
                }
            }
            else if (tearOffUi.window)
            {
                vkvsg::VsgInputManager::WindowRect mainRect;
                vkvsg::VsgInputManager::WindowRect tearOffRect;
                if (inputManager->getWindowRect(window, mainRect) && inputManager->getWindowRect(tearOffUi.window, tearOffRect))
                {
                    if (appState->suppressAutoDockUntilTearOffMove)
                    {
                        const int dx = std::abs(tearOffRect.x - appState->tearOffInitialRect.x);
                        const int dy = std::abs(tearOffRect.y - appState->tearOffInitialRect.y);
                        if (dx > 8 || dy > 8) appState->suppressAutoDockUntilTearOffMove = false;
                    }

                    if (!appState->suppressAutoDockUntilTearOffMove &&
                        computeWindowOverlapRatio(tearOffRect, mainRect) >= kTearOffOverlapThreshold)
                    {
                        pendingDestroyTearOffWindow = true;
                    }
                }
            }

            if (inputManager->consumeWireframeToggleRequest())
            {
                appState->wireframe = !appState->wireframe;
                globeTransform->children.clear();
                bool loadedTexture = false;
                auto rebuilt = vkvsg::createGlobeNode(earthTexturePath, appState->wireframe, kWgs84EquatorialRadiusFeet, kWgs84PolarRadiusFeet, loadedTexture);
                appState->textureFromFile = loadedTexture;
                if (!rebuilt)
                {
                    std::cerr << "Failed to rebuild globe node." << std::endl;
                    return 1;
                }
                globeTransform->addChild(rebuilt);
                auto rebuiltEquator = vkvsg::createEquatorLineNode(kWgs84EquatorialRadiusFeet);
                if (rebuiltEquator) globeTransform->addChild(rebuiltEquator);
            }

            appState->ui.deltaTimeMs = 1000.0f * delta;
            appState->ui.fps = (delta > 0.0f) ? (1.0f / delta) : 0.0f;
            appState->ui.gpuFrameMs = static_cast<float>(latestVsgGpuFrameMs(*profiler));

            viewer->update();
            viewer->recordAndSubmit();
            viewer->present();

            if (pendingDestroyTearOffWindow)
            {
                destroyTearOffWindow();
                pendingDestroyTearOffWindow = false;
                pendingCreateTearOffWindow = false;
            }
            else if (pendingCreateTearOffWindow)
            {
                createTearOffWindow();
                pendingCreateTearOffWindow = false;
            }
        }

        destroyTearOffWindow();
        destroyUiWindowResources(mainUi);
        profiler->finish();
        appState->ui.gpuFrameMs = static_cast<float>(latestVsgGpuFrameMs(*profiler));

        std::cout << "[EXIT] vsgdock status=OK code=0"
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
        std::cout << "[EXIT] vsgdock status=FAIL code=1 reason=\"" << e.message << "\"" << std::endl;
        std::cerr << "[vsg::Exception] " << e.message << std::endl;
        return 1;
    }
    catch (const std::exception& e)
    {
        std::cout << "[EXIT] vsgdock status=FAIL code=1 reason=\"" << e.what() << "\"" << std::endl;
        std::cerr << "[Exception] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
