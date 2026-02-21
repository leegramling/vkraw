#pragma once

#include "vkvsg/CubeObject.h"

#include <vsgImGui/imgui.h>

namespace vkvsg {

class UIObject {
public:
    bool showDemoWindow = true;
    float deltaTimeMs = 0.0f;
    float fps = 0.0f;
    float gpuFrameMs = 0.0f;
    const char* presentModeName = "IMMEDIATE (requested)";

    bool draw(CubeObject& cube)
    {
        ImGui::Begin("Cube Controls");
        ImGui::Text("Arrow keys rotate the cube");
        ImGui::SliderFloat("Yaw", &cube.yaw, -180.0f, 180.0f);
        ImGui::SliderFloat("Pitch", &cube.pitch, -89.0f, 89.0f);
        ImGui::SliderFloat("Auto spin (deg/s)", &cube.autoSpinDegPerSec, -180.0f, 180.0f);
        const bool changedCount = ImGui::SliderInt("Cube count", &cube.cubeCount, 20000, 100000);
        ImGui::Text("FPS %.1f", fps);
        ImGui::Text("Frame time %.3f ms", deltaTimeMs);
        ImGui::Text("Triangles %llu", static_cast<unsigned long long>(cube.triangles()));
        ImGui::Text("Vertices %llu", static_cast<unsigned long long>(cube.vertices()));
        ImGui::Text("Present mode %s", presentModeName);
        ImGui::Text("GPU frame %.3f ms", gpuFrameMs);
        ImGui::End();

        ImGui::ShowDemoWindow(&showDemoWindow);
        return changedCount;
    }
};

} // namespace vkvsg
