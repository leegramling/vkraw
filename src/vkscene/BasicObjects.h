#pragma once

#include "vkscene/RenderObject.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>

namespace vkscene {

class TriangleObject final : public RenderObject {
public:
    TriangleObject()
        : RenderObject("TriangleObject",
                       PrimitiveType::Triangles,
                       ShaderSet{"cube.vert.spv", "cube.frag.spv"},
                       Material{.textureSlot = 0, .baseColor = glm::vec4(1.0f)}) {}

    void buildMesh(std::vector<core::Vertex>& outVertices,
                   std::vector<uint32_t>& outIndices) const override
    {
        outVertices = {
            {{-35.0f, -20.0f, 0.0f}, {1.0f, 0.2f, 0.2f}, {0.0f, 0.0f}},
            {{35.0f, -20.0f, 0.0f}, {0.2f, 1.0f, 0.2f}, {1.0f, 0.0f}},
            {{0.0f, 35.0f, 0.0f}, {0.2f, 0.4f, 1.0f}, {0.5f, 1.0f}},
        };
        outIndices = {0, 1, 2};
    }

    void update(float /*deltaSeconds*/, float elapsedSeconds) override
    {
        glm::mat4 model(1.0f);
        model = glm::rotate(model, elapsedSeconds * 0.8f, glm::vec3(0.0f, 1.0f, 0.0f));
        setModelMatrix(model);
    }
};

class LineCircleObject final : public RenderObject {
public:
    explicit LineCircleObject(uint32_t segments = 96, float radius = 80.0f)
        : RenderObject("LineCircleObject",
                       PrimitiveType::Lines,
                       ShaderSet{"cube.vert.spv", "cube.frag.spv"},
                       Material{.textureSlot = 0, .baseColor = glm::vec4(1.0f)}),
          segments_(segments),
          radius_(radius) {}

    void buildMesh(std::vector<core::Vertex>& outVertices,
                   std::vector<uint32_t>& outIndices) const override
    {
        const uint32_t seg = segments_ < 3 ? 3 : segments_;
        outVertices.clear();
        outIndices.clear();
        outVertices.reserve(seg);
        outIndices.reserve(seg * 2);

        for (uint32_t i = 0; i < seg; ++i) {
            const float t = (static_cast<float>(i) / static_cast<float>(seg)) * glm::two_pi<float>();
            const float x = std::cos(t) * radius_;
            const float z = std::sin(t) * radius_;
            outVertices.push_back({{x, 0.0f, z}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}});
        }

        for (uint32_t i = 0; i < seg; ++i) {
            const uint32_t j = (i + 1) % seg;
            outIndices.push_back(i);
            outIndices.push_back(j);
        }
    }

private:
    uint32_t segments_ = 96;
    float radius_ = 80.0f;
};

} // namespace vkscene
