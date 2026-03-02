#pragma once

#include <imgui.h>

namespace vkraw {

class UIObject {
public:
    bool showDemoWindow = true;
    float fps = 0.0f;
    float frameTimeMs = 0.0f;
    float gpuFrameMs = 0.0f;

    bool draw(const char* presentMode, bool gpuTimingAvailable, size_t sceneNodeCount, size_t visibleSceneNodes, size_t ecsEntities,
              size_t ecsVisible, bool sceneModeEnabled, bool& requestExit)
    {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) requestExit = true;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        ImGui::Begin(sceneModeEnabled ? "Scene Controls" : "App Controls");
        if (sceneModeEnabled) {
            ImGui::TextUnformatted("vkScene mode");
            ImGui::TextUnformatted("RenderObject scene enabled");
        } else {
            ImGui::TextUnformatted("vkraw mode");
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
        return false;
    }
};

} // namespace vkraw
