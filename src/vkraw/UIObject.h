#pragma once

#include "vkraw/CubeObject.h"

#include <imgui.h>

namespace vkraw {

class UIObject {
public:
    bool showDemoWindow = true;
    float fps = 0.0f;
    float frameTimeMs = 0.0f;
    float gpuFrameMs = 0.0f;

    bool draw(CubeObject& cube, const char* presentMode, bool gpuTimingAvailable)
    {
        ImGui::Begin("Cube Controls");
        ImGui::Text("Arrow keys rotate the cube");
        ImGui::SliderFloat("Yaw", &cube.yaw, -180.0f, 180.0f);
        ImGui::SliderFloat("Pitch", &cube.pitch, -89.0f, 89.0f);
        ImGui::SliderFloat("Auto spin (deg/s)", &cube.autoSpinSpeedDeg, -180.0f, 180.0f);
        const bool changedCount = ImGui::SliderInt("Cube count", &cube.cubeCount, 20000, 100000);
        ImGui::Text("FPS %.1f", fps);
        ImGui::Text("Frame time %.3f ms", frameTimeMs);
        ImGui::Text("Triangles %llu", static_cast<unsigned long long>(cube.triangles()));
        ImGui::Text("Vertices %llu", static_cast<unsigned long long>(cube.vertices()));
        ImGui::Text("Present mode %s", presentMode);
        if (gpuTimingAvailable) {
            ImGui::Text("GPU frame %.3f ms", gpuFrameMs);
        } else {
            ImGui::TextUnformatted("GPU frame n/a (timestamps unsupported)");
        }
        ImGui::End();

        ImGui::ShowDemoWindow(&showDemoWindow);
        return changedCount;
    }
};

} // namespace vkraw
