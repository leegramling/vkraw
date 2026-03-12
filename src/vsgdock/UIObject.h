#pragma once

#include <vsgImGui/imgui.h>

namespace vkvsg {

class UIObject {
public:
    struct PanelLayout {
        ImVec2 pos{0.0f, 0.0f};
        ImVec2 size{0.0f, 0.0f};
        bool moving = false;
    };

    bool showDemoWindow = true;
    float deltaTimeMs = 0.0f;
    float fps = 0.0f;
    float gpuFrameMs = 0.0f;
    const char* presentModeName = "IMMEDIATE (requested)";

    void drawGlobeControls(bool wireframeEnabled,
                           bool textureFromFile,
                           PanelLayout* layout = nullptr,
                           ImGuiWindowFlags flags = 0,
                           bool showDockBackButton = false,
                           bool* dockBackRequested = nullptr)
    {
        ImGui::Begin("Globe Controls", nullptr, flags);
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

    void drawDemo()
    {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }

    void draw(bool wireframeEnabled, bool textureFromFile)
    {
        drawGlobeControls(wireframeEnabled, textureFromFile);
        drawDemo();
    }
};

} // namespace vkvsg
