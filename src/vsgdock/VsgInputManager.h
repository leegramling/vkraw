#pragma once

#include <vsg/all.h>
#include <vsgImGui/imgui.h>

#include <chrono>
#include <unordered_map>
#include <vector>

namespace vkvsg {

class VsgInputManager : public vsg::Inherit<vsg::Visitor, VsgInputManager>
{
public:
    explicit VsgInputManager(vsg::ref_ptr<vsg::Viewer> viewer);

    void setMainWindow(vsg::ref_ptr<vsg::Window> window);
    void setTearOffWindow(vsg::ref_ptr<vsg::Window> window);
    void addWindow(vsg::ref_ptr<vsg::Window> window, ImGuiContext* context);
    void removeWindow(vsg::ref_ptr<vsg::Window> window);

    void apply(vsg::MoveEvent& moveEvent) override;
    void apply(vsg::ButtonPressEvent& buttonPress) override;
    void apply(vsg::ButtonReleaseEvent& buttonRelease) override;
    void apply(vsg::ScrollWheelEvent& scrollWheel) override;
    void apply(vsg::KeyPressEvent& keyPress) override;
    void apply(vsg::KeyReleaseEvent& keyRelease) override;
    void apply(vsg::ConfigureWindowEvent& configureWindow) override;
    void apply(vsg::FrameEvent& frameEvent) override;
    void apply(vsg::CloseWindowEvent& closeWindowEvent) override;

    void processQueuedEvents();

    bool consumeWireframeToggleRequest();
    bool consumeTearOffCloseRequest();
    bool leftMouseButtonDown() const;

private:
    struct ImGuiWindowState
    {
        vsg::observer_ptr<vsg::Window> window;
        ImGuiContext* context = nullptr;
        bool draggingScene = false;
        std::chrono::high_resolution_clock::time_point lastFrameTime = std::chrono::high_resolution_clock::now();
    };

    struct ScopedImGuiContext
    {
        explicit ScopedImGuiContext(ImGuiContext* next);
        ~ScopedImGuiContext();

        ImGuiContext* previous = nullptr;
    };

    ImGuiWindowState* getState(vsg::Window* window);
    void processEvent(vsg::UIEvent& event);

    static uint32_t convertButton(uint32_t button);
    static void updateModifier(ImGuiIO& io, vsg::KeyModifier& modifier, bool pressed);
    static ImGuiKey convertKey(vsg::KeySymbol keyBase, vsg::KeySymbol keyModified);

    std::unordered_map<vsg::Window*, ImGuiWindowState> windows;
    vsg::ref_ptr<vsg::Viewer> viewer;
    vsg::ref_ptr<vsg::Window> mainWindow;
    vsg::ref_ptr<vsg::Window> tearOffWindow;
    bool wireframeToggleRequested = false;
    bool tearOffCloseRequested = false;
    bool leftButtonDown = false;
};

} // namespace vkvsg
