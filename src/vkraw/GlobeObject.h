#pragma once

#include "vkraw/CubeRenderTypes.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vkraw {

class GlobeObject {
public:
    float yaw = 0.0f;
    float pitch = 0.0f;
    float autoSpinSpeedDeg = 0.0f;
    int latitudeSegments = 128;
    int longitudeSegments = 256;
    int tileRows = 6;
    int tileCols = 12;
    float radius = 100.0f;
    float mouseRotateDegreesPerPixel = 0.20f;

    void processInput(GLFWwindow* window, float deltaSeconds)
    {
        constexpr float rotateSpeed = 90.0f;
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) yaw -= rotateSpeed * deltaSeconds;
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) yaw += rotateSpeed * deltaSeconds;
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) pitch -= rotateSpeed * deltaSeconds;
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) pitch += rotateSpeed * deltaSeconds;

        const int lmbPressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        if (lmbPressed == GLFW_PRESS)
        {
            if (mouseDragActive_)
            {
                const float dx = static_cast<float>(mouseX - lastMouseX_);
                const float dy = static_cast<float>(mouseY - lastMouseY_);
                yaw += dx * mouseRotateDegreesPerPixel;
                pitch += dy * mouseRotateDegreesPerPixel;
            }
            mouseDragActive_ = true;
            lastMouseX_ = mouseX;
            lastMouseY_ = mouseY;
        }
        else
        {
            mouseDragActive_ = false;
        }

        pitch = std::clamp(pitch, -89.0f, 89.0f);
    }

    glm::mat4 computeBaseRotation(float elapsedSeconds) const
    {
        const float autoYaw = yaw + autoSpinSpeedDeg * elapsedSeconds;
        glm::mat4 model(1.0f);
        model = glm::rotate(model, glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(autoYaw), glm::vec3(0.0f, 1.0f, 0.0f));
        return model;
    }

    void rebuildMesh(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) const
    {
        const uint32_t latSeg = static_cast<uint32_t>(std::max(8, latitudeSegments));
        const uint32_t lonSeg = static_cast<uint32_t>(std::max(16, longitudeSegments));
        const uint32_t tilesLat = static_cast<uint32_t>(std::clamp(tileRows, 1, std::max(1, latitudeSegments)));
        const uint32_t tilesLon = static_cast<uint32_t>(std::clamp(tileCols, 1, std::max(1, longitudeSegments)));

        const uint32_t rows = latSeg + 1;
        const uint32_t cols = lonSeg + 1;

        vertices.clear();
        indices.clear();
        vertices.reserve(static_cast<size_t>(rows) * cols);
        indices.reserve(static_cast<size_t>(latSeg) * lonSeg * 6);
        std::vector<uint32_t> globalVertexMap(static_cast<size_t>(rows) * cols, UINT32_MAX);

        auto getOrCreateVertex = [&](uint32_t r, uint32_t c) -> uint32_t
        {
            const size_t mapIndex = static_cast<size_t>(r) * cols + c;
            if (globalVertexMap[mapIndex] != UINT32_MAX) return globalVertexMap[mapIndex];

            const float v = static_cast<float>(r) / static_cast<float>(latSeg);
            const float u = static_cast<float>(c) / static_cast<float>(lonSeg);
            const float lat = (0.5f - v) * glm::pi<float>();
            const float lon = (u * 2.0f - 1.0f) * glm::pi<float>();
            const float cosLat = std::cos(lat);
            const float sinLat = std::sin(lat);
            const float cosLon = std::cos(lon);
            const float sinLon = std::sin(lon);

            glm::vec3 normal(cosLat * cosLon, sinLat, cosLat * sinLon);
            glm::vec3 pos = normal * radius;
            glm::vec3 color(1.0f, 1.0f, 1.0f);
            glm::vec2 uv(u, 1.0f - v);

            const uint32_t index = static_cast<uint32_t>(vertices.size());
            vertices.push_back(Vertex{pos, color, uv});
            globalVertexMap[mapIndex] = index;
            return index;
        };

        for (uint32_t tileR = 0; tileR < tilesLat; ++tileR)
        {
            const uint32_t rStart = (tileR * latSeg) / tilesLat;
            const uint32_t rEnd = ((tileR + 1) * latSeg) / tilesLat;
            for (uint32_t tileC = 0; tileC < tilesLon; ++tileC)
            {
                const uint32_t cStart = (tileC * lonSeg) / tilesLon;
                const uint32_t cEnd = ((tileC + 1) * lonSeg) / tilesLon;

                for (uint32_t r = rStart; r < rEnd; ++r)
                {
                    for (uint32_t c = cStart; c < cEnd; ++c)
                    {
                        const uint32_t i00 = getOrCreateVertex(r, c);
                        const uint32_t i01 = getOrCreateVertex(r, c + 1);
                        const uint32_t i10 = getOrCreateVertex(r + 1, c);
                        const uint32_t i11 = getOrCreateVertex(r + 1, c + 1);

                        indices.push_back(i00);
                        indices.push_back(i01);
                        indices.push_back(i10);

                        indices.push_back(i10);
                        indices.push_back(i01);
                        indices.push_back(i11);
                    }
                }
            }
        }
    }

    uint64_t triangles() const
    {
        return static_cast<uint64_t>(std::max(8, latitudeSegments)) *
               static_cast<uint64_t>(std::max(16, longitudeSegments)) * 2ULL;
    }

    uint64_t vertices() const
    {
        return static_cast<uint64_t>(std::max(8, latitudeSegments) + 1) *
               static_cast<uint64_t>(std::max(16, longitudeSegments) + 1);
    }

private:
    bool mouseDragActive_ = false;
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
};

} // namespace vkraw
