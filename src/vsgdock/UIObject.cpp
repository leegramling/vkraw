#include "vsgdock/UIObject.h"

#include <imgui_internal.h>

namespace vkvsg {

namespace
{
constexpr const char* kGlobeControlsWindow = "Globe Controls";
constexpr const char* kSettingsWindow = "Settings";
constexpr const char* kCursorWindow = "Cursor";
constexpr const char* kFpsWindow = "FPS";
constexpr const char* kDemoWindow = "Dear ImGui Demo";

ImGuiID dockNodeForPreset(ImGuiID topLeft,
                          ImGuiID topRight,
                          ImGuiID center,
                          ImGuiID bottomLeft,
                          ImGuiID bottomRight,
                          UIObject::DockPreset preset)
{
    switch (preset)
    {
        case UIObject::DockPreset::TopLeft: return topLeft;
        case UIObject::DockPreset::TopRight: return topRight;
        case UIObject::DockPreset::Center: return center;
        case UIObject::DockPreset::BottomLeft: return bottomLeft;
        case UIObject::DockPreset::BottomRight: return bottomRight;
    }
    return center;
}
} // namespace

void UIObject::drawMainMenu(bool* exitRequested)
{
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File"))
    {
        if (exitRequested && ImGui::MenuItem("Exit"))
        {
            *exitRequested = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window"))
    {
        ImGui::MenuItem("Settings", nullptr, &showSettingsPanel);
        ImGui::MenuItem("Cursor", nullptr, &showCursorPanel);
        ImGui::MenuItem("FPS", nullptr, &showFpsPanel);
        ImGui::MenuItem("Demo", nullptr, &showDemoWindow);
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void UIObject::applyDockLayout(ImGuiID dockspaceId)
{
    if (!dockLayoutDirty || dockspaceId == 0) return;

    ImGui::DockBuilderRemoveNodeChildNodes(dockspaceId);

    ImGuiID center = dockspaceId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.24f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);

    ImGuiID topLeft = ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.5f, nullptr, &left);
    ImGuiID bottomLeft = left;
    ImGuiID topRight = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.5f, nullptr, &right);
    ImGuiID bottomRight = right;

    ImGui::DockBuilderDockWindow(kSettingsWindow,
                                 dockNodeForPreset(topLeft, topRight, center, bottomLeft, bottomRight, settingsPanelDock));
    ImGui::DockBuilderDockWindow(kCursorWindow,
                                 dockNodeForPreset(topLeft, topRight, center, bottomLeft, bottomRight, cursorPanelDock));
    ImGui::DockBuilderDockWindow(kFpsWindow,
                                 dockNodeForPreset(topLeft, topRight, center, bottomLeft, bottomRight, fpsPanelDock));
    ImGui::DockBuilderFinish(dockspaceId);

    dockLayoutDirty = false;
}

void UIObject::drawGlobeControls(bool wireframeEnabled,
                                 bool textureFromFile,
                                 PanelLayout* layout,
                                 ImGuiWindowFlags flags,
                                 bool showDockBackButton,
                                 bool* dockBackRequested)
{
    ImGui::Begin(kGlobeControlsWindow, nullptr, flags);
    ImGui::Text("LMB drag: rotate globe at origin");
    ImGui::Text("Wheel: zoom camera");
    ImGui::Text("Press W to toggle wireframe");
    ImGui::Text("Wireframe: %s", wireframeEnabled ? "ON" : "OFF");
    ImGui::Text("Texture source: %s", textureFromFile ? "Image file" : "Procedural fallback");
    ImGui::Text("FPS %.1f", fps);
    ImGui::Text("Frame time %.3f ms", deltaTimeMs);
    ImGui::Text("Present mode %s", presentModeName);
    ImGui::Text("GPU frame %.3f ms", gpuFrameMs);
    if (showDockBackButton && dockBackRequested && ImGui::Button("Dock Back"))
    {
        *dockBackRequested = true;
    }
    if (layout)
    {
        layout->pos = ImGui::GetWindowPos();
        layout->size = ImGui::GetWindowSize();
        layout->moving = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    }
    ImGui::End();
}

void UIObject::drawSettingsPanel()
{
    if (!showSettingsPanel) return;

    if (ImGui::Begin(kSettingsWindow, &showSettingsPanel))
    {
        bool changed = false;
        changed |= drawDockPresetCombo("Settings Dock", settingsPanelDock);
        changed |= drawDockPresetCombo("Cursor Dock", cursorPanelDock);
        changed |= drawDockPresetCombo("FPS Dock", fpsPanelDock);

        ImGui::Separator();
        ImGui::Checkbox("Show Cursor Panel", &showCursorPanel);
        ImGui::Checkbox("Show FPS Panel", &showFpsPanel);
        ImGui::Checkbox("Show Demo Window", &showDemoWindow);

        if (changed) dockLayoutDirty = true;
    }
    ImGui::End();
}

void UIObject::drawCursorPanel()
{
    if (!showCursorPanel) return;

    if (ImGui::Begin(kCursorWindow, &showCursorPanel, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        ImGui::Text("x: %.1f", mouse.x);
        ImGui::Text("y: %.1f", mouse.y);
    }
    ImGui::End();
}

void UIObject::drawFpsPanel()
{
    if (!showFpsPanel) return;

    if (ImGui::Begin(kFpsWindow, &showFpsPanel, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("FPS %.1f", fps);
        ImGui::Text("CPU %.3f ms", deltaTimeMs);
        ImGui::Text("GPU %.3f ms", gpuFrameMs);
    }
    ImGui::End();
}

void UIObject::drawDemo()
{
    if (showDemoWindow)
    {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }
}

const char* UIObject::dockPresetLabel(DockPreset preset)
{
    switch (preset)
    {
        case DockPreset::TopLeft: return "Top Left";
        case DockPreset::TopRight: return "Top Right";
        case DockPreset::Center: return "Center";
        case DockPreset::BottomLeft: return "Bottom Left";
        case DockPreset::BottomRight: return "Bottom Right";
    }
    return "Center";
}

bool UIObject::drawDockPresetCombo(const char* label, DockPreset& value)
{
    bool changed = false;
    if (ImGui::BeginCombo(label, dockPresetLabel(value)))
    {
        for (int i = static_cast<int>(DockPreset::TopLeft); i <= static_cast<int>(DockPreset::BottomRight); ++i)
        {
            auto preset = static_cast<DockPreset>(i);
            const bool selected = (preset == value);
            if (ImGui::Selectable(dockPresetLabel(preset), selected))
            {
                value = preset;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace vkvsg
