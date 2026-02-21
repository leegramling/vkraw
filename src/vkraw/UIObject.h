#pragma once

#include "vkraw/GlobeObject.h"

#include <imgui.h>

namespace vkraw {

class UIObject {
public:
    bool showDemoWindow = true;
    float fps = 0.0f;
    float frameTimeMs = 0.0f;
    float gpuFrameMs = 0.0f;

    bool draw(GlobeObject& globe, const char* presentMode, bool gpuTimingAvailable, size_t sceneNodeCount, size_t visibleSceneNodes, size_t ecsEntities,
              size_t ecsVisible)
    {
        ImGui::Begin("Globe Controls");
        ImGui::Text("LMB drag rotates globe (origin-anchored)");
        ImGui::Text("Arrow keys also rotate");
        ImGui::SliderFloat("Yaw", &globe.yaw, -180.0f, 180.0f);
        ImGui::SliderFloat("Pitch", &globe.pitch, -89.0f, 89.0f);
        ImGui::SliderFloat("Auto spin (deg/s)", &globe.autoSpinSpeedDeg, -180.0f, 180.0f);
        const bool changedLat = ImGui::SliderInt("Latitude segments", &globe.latitudeSegments, 32, 512);
        const bool changedLon = ImGui::SliderInt("Longitude segments", &globe.longitudeSegments, 64, 1024);
        const bool changedTileRows = ImGui::SliderInt("Tile rows", &globe.tileRows, 1, 32);
        const bool changedTileCols = ImGui::SliderInt("Tile cols", &globe.tileCols, 1, 64);
        const bool changedRadius = ImGui::SliderFloat("Radius", &globe.radius, 10.0f, 300.0f);
        ImGui::SliderFloat("Mouse rotate deg/pixel", &globe.mouseRotateDegreesPerPixel, 0.02f, 1.00f);
        ImGui::Text("FPS %.1f", fps);
        ImGui::Text("Frame time %.3f ms", frameTimeMs);
        ImGui::Text("Triangles %llu", static_cast<unsigned long long>(globe.triangles()));
        ImGui::Text("Vertices %llu", static_cast<unsigned long long>(globe.vertices()));
        ImGui::Text("SceneGraph nodes %llu", static_cast<unsigned long long>(sceneNodeCount));
        ImGui::Text("SceneGraph visible %llu", static_cast<unsigned long long>(visibleSceneNodes));
        ImGui::Text("ECS entities %llu", static_cast<unsigned long long>(ecsEntities));
        ImGui::Text("ECS visible %llu", static_cast<unsigned long long>(ecsVisible));
        ImGui::Text("Present mode %s", presentMode);
        if (gpuTimingAvailable) {
            ImGui::Text("GPU frame %.3f ms", gpuFrameMs);
        } else {
            ImGui::TextUnformatted("GPU frame n/a (timestamps unsupported)");
        }
        ImGui::End();

        ImGui::ShowDemoWindow(&showDemoWindow);
        return changedLat || changedLon || changedTileRows || changedTileCols || changedRadius;
    }
};

} // namespace vkraw
