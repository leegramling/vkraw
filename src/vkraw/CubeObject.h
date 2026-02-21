#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace vkraw {

class CubeObject {
public:
    float yaw = 30.0f;
    float pitch = 20.0f;
    float autoSpinSpeedDeg = 22.5f;
    int cubeCount = 100000;

    void rebuildOffsets()
    {
        offsets.clear();
        offsets.reserve(static_cast<size_t>(cubeCount));

        const int side = std::max(1, static_cast<int>(std::ceil(std::cbrt(static_cast<float>(cubeCount)))));
        const float spacing = 2.8f;
        const glm::vec3 centerOffset(
            0.5f * static_cast<float>(side - 1),
            0.5f * static_cast<float>(side - 1),
            0.5f * static_cast<float>(side - 1));

        for (int i = 0; i < cubeCount; ++i) {
            const int x = i % side;
            const int y = (i / side) % side;
            const int z = i / (side * side);
            const glm::vec3 gridPos(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
            offsets.push_back((gridPos - centerOffset) * spacing);
        }
    }

    void processInput(GLFWwindow* window, float deltaSeconds)
    {
        constexpr float rotateSpeed = 90.0f;
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) yaw -= rotateSpeed * deltaSeconds;
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) yaw += rotateSpeed * deltaSeconds;
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) pitch += rotateSpeed * deltaSeconds;
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) pitch -= rotateSpeed * deltaSeconds;
    }

    glm::mat4 computeBaseRotation(float elapsedSeconds) const
    {
        const float autoYaw = yaw + autoSpinSpeedDeg * elapsedSeconds;
        glm::mat4 model(1.0f);
        model = glm::rotate(model, glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(autoYaw), glm::vec3(0.0f, 1.0f, 0.0f));
        return model;
    }

    uint64_t triangles() const { return static_cast<uint64_t>(cubeCount) * 12ULL; }
    uint64_t vertices() const { return static_cast<uint64_t>(cubeCount) * 8ULL; }

    std::vector<glm::vec3> offsets;
};

} // namespace vkraw
