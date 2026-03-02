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
              size_t ecsVisible, bool sceneModeEnabled, bool& requestExit)
    {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) requestExit = true;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        ImGui::Begin(sceneModeEnabled ? "Scene Controls" : "Globe Controls");
        bool changedLat = false;
        bool changedLon = false;
        bool changedTileRows = false;
        bool changedTileCols = false;
        bool changedRadius = false;

        if (!sceneModeEnabled) {
            ImGui::Text("LMB drag rotates globe (origin-anchored)");
            ImGui::Text("Arrow keys also rotate");
            ImGui::SliderFloat("Yaw", &globe.yaw, -180.0f, 180.0f);
            ImGui::SliderFloat("Pitch", &globe.pitch, -89.0f, 89.0f);
            ImGui::SliderFloat("Auto spin (deg/s)", &globe.autoSpinSpeedDeg, -180.0f, 180.0f);
            changedLat = ImGui::SliderInt("Latitude segments", &globe.latitudeSegments, 32, 512);
            changedLon = ImGui::SliderInt("Longitude segments", &globe.longitudeSegments, 64, 1024);
            changedTileRows = ImGui::SliderInt("Tile rows", &globe.tileRows, 1, 32);
            changedTileCols = ImGui::SliderInt("Tile cols", &globe.tileCols, 1, 64);
            changedRadius = ImGui::SliderFloat("Radius", &globe.radius, 10.0f, 300.0f);
            ImGui::SliderFloat("Mouse rotate deg/pixel", &globe.mouseRotateDegreesPerPixel, 0.02f, 1.00f);
            ImGui::Text("Triangles %llu", static_cast<unsigned long long>(globe.triangles()));
            ImGui::Text("Vertices %llu", static_cast<unsigned long long>(globe.vertices()));
        } else {
            ImGui::TextUnformatted("vkScene mode");
            ImGui::TextUnformatted("RenderObject scene enabled");
        }

        ImGui::Text("FPS %.1f", fps);
        ImGui::Text("Frame time %.3f ms", frameTimeMs);
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
