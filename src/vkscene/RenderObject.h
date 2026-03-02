#pragma once

#include "vkraw/CubeRenderTypes.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace vkscene {

enum class PrimitiveType {
    Triangles,
    Lines
};

struct ShaderSet {
    std::string vertexShaderSpv;
    std::string fragmentShaderSpv;
};

class RenderObject {
public:
    virtual ~RenderObject() = default;

    const std::string& name() const { return name_; }
    PrimitiveType primitive() const { return primitive_; }
    const ShaderSet& shaders() const { return shaders_; }
    const glm::mat4& modelMatrix() const { return modelMatrix_; }

    virtual void buildMesh(std::vector<vkraw::Vertex>& outVertices,
                           std::vector<uint32_t>& outIndices) const = 0;

    virtual void update(float /*deltaSeconds*/, float /*elapsedSeconds*/) {}

protected:
    explicit RenderObject(std::string name, PrimitiveType primitive, ShaderSet shaders)
        : name_(std::move(name)), primitive_(primitive), shaders_(std::move(shaders)) {}

    void setModelMatrix(const glm::mat4& model) { modelMatrix_ = model; }

private:
    std::string name_;
    PrimitiveType primitive_ = PrimitiveType::Triangles;
    ShaderSet shaders_{};
    glm::mat4 modelMatrix_{1.0f};
};

using RenderObjectPtr = std::shared_ptr<RenderObject>;

} // namespace vkscene

