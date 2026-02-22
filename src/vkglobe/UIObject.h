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

    void draw(bool wireframeEnabled, bool textureFromFile, bool osmEnabled, bool osmActive, int osmZoom, double osmAltitudeFt,
              size_t osmVisibleTiles, size_t osmCachedTiles)
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
        ImGui::Separator();
        ImGui::Text("OSM: %s", osmEnabled ? "enabled" : "disabled");
        ImGui::Text("OSM active: %s", osmActive ? "yes" : "no");
        ImGui::Text("OSM zoom: %d", osmZoom);
        ImGui::Text("OSM altitude: %.1f ft", osmAltitudeFt);
        ImGui::Text("OSM visible tiles: %llu", static_cast<unsigned long long>(osmVisibleTiles));
        ImGui::Text("OSM cached tiles: %llu", static_cast<unsigned long long>(osmCachedTiles));
        ImGui::End();

        ImGui::ShowDemoWindow(&showDemoWindow);
    }
};

} // namespace vkglobe
