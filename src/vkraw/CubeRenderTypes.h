#pragma once

#include "core/RenderTypes.h"

#include <array>
#include <cstdint>

namespace vkraw {

using Vertex = core::Vertex;
using UniformBufferObject = core::UniformBufferObject;
using PushConstantData = core::PushConstantData;

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
