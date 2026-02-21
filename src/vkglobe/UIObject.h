#pragma once

#include <vsgImGui/imgui.h>

namespace vkglobe {

class UIObject {
public:
    bool showDemoWindow = true;
    float deltaTimeMs = 0.0f;
    float fps = 0.0f;
    float gpuFrameMs = 0.0f;
    const char* presentModeName = "IMMEDIATE (requested)";

    void draw(bool wireframeEnabled, bool textureFromFile)
    {
        ImGui::Begin("Globe Controls");
        ImGui::Text("LMB drag: rotate globe at origin");
        ImGui::Text("Wheel: zoom camera");
        ImGui::Text("Press W to toggle wireframe");
        ImGui::Text("Wireframe: %s", wireframeEnabled ? "ON" : "OFF");
        ImGui::Text("Texture source: %s", textureFromFile ? "Image file" : "Procedural fallback");
        ImGui::Text("FPS %.1f", fps);
        ImGui::Text("Frame time %.3f ms", deltaTimeMs);
        ImGui::Text("Present mode %s", presentModeName);
        ImGui::Text("GPU frame %.3f ms", gpuFrameMs);
        ImGui::End();

        ImGui::ShowDemoWindow(&showDemoWindow);
    }
};

} // namespace vkglobe
