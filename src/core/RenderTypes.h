#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace core {

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

struct ObjectUniformData {
    glm::mat4 model;
    glm::uvec4 material;
};

struct PushConstantData {
    alignas(16) uint32_t objectIndex = 0;
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
    uint32_t _pad2 = 0;
};

} // namespace core
