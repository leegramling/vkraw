#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace vkraw {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 uv;

    static VkVertexInputBindingDescription bindingDescription() {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> attributes{};
        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(Vertex, pos);

        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(Vertex, color);

        attributes[2].binding = 0;
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[2].offset = offsetof(Vertex, uv);
        return attributes;
    }
};

struct UniformBufferObject {
    glm::mat4 viewProj;
};

struct PushConstantData {
    glm::mat4 model;
};

inline const std::array<Vertex, 8> kVertices = {{
    {{-1.0f, -1.0f, -1.0f}, {1.0f, 0.2f, 0.2f}, {0.0f, 0.0f}},
    {{1.0f, -1.0f, -1.0f}, {0.2f, 1.0f, 0.2f}, {1.0f, 0.0f}},
    {{1.0f, 1.0f, -1.0f}, {0.2f, 0.2f, 1.0f}, {1.0f, 1.0f}},
    {{-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, 0.2f}, {0.0f, 1.0f}},
    {{-1.0f, -1.0f, 1.0f}, {1.0f, 0.2f, 1.0f}, {0.0f, 0.0f}},
    {{1.0f, -1.0f, 1.0f}, {0.2f, 1.0f, 1.0f}, {1.0f, 0.0f}},
    {{1.0f, 1.0f, 1.0f}, {0.9f, 0.9f, 0.9f}, {1.0f, 1.0f}},
    {{-1.0f, 1.0f, 1.0f}, {0.5f, 0.5f, 0.9f}, {0.0f, 1.0f}},
}};

inline const std::array<uint16_t, 36> kIndices = {{
    0, 1, 2, 2, 3, 0,
    4, 5, 6, 6, 7, 4,
    0, 4, 7, 7, 3, 0,
    1, 5, 6, 6, 2, 1,
    3, 2, 6, 6, 7, 3,
    0, 1, 5, 5, 4, 0,
}};

} // namespace vkraw
