#pragma once

#include <vsgImGui/imgui.h>

namespace vkvsg {

class UIObject
{
public:
    struct PanelLayout
    {
        ImVec2 pos{0.0f, 0.0f};
        ImVec2 size{0.0f, 0.0f};
        bool moving = false;
    };

    enum class DockPreset
    {
        TopLeft = 0,
        TopRight,
        Center,
        BottomLeft,
        BottomRight
    };

    bool showDemoWindow = true;
    bool showSettingsPanel = true;
    bool showCursorPanel = true;
    bool showFpsPanel = true;

    float deltaTimeMs = 0.0f;
    float fps = 0.0f;
    float gpuFrameMs = 0.0f;
    const char* presentModeName = "IMMEDIATE (requested)";

    DockPreset cursorPanelDock = DockPreset::TopLeft;
    DockPreset fpsPanelDock = DockPreset::BottomRight;
    DockPreset settingsPanelDock = DockPreset::TopRight;

    void drawMainMenu(bool* exitRequested);
    void applyDockLayout(ImGuiID dockspaceId);
    void drawGlobeControls(bool wireframeEnabled,
                           bool textureFromFile,
                           PanelLayout* layout = nullptr,
                           ImGuiWindowFlags flags = 0,
                           bool showDockBackButton = false,
                           bool* dockBackRequested = nullptr);
    void drawSettingsPanel();
    void drawCursorPanel();
    void drawFpsPanel();
    void drawDemo();

private:
    static const char* dockPresetLabel(DockPreset preset);
    bool drawDockPresetCombo(const char* label, DockPreset& value);

    bool dockLayoutDirty = true;
};

} // namespace vkvsg
