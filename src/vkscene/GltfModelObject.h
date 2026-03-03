#pragma once

#include "vkscene/RenderObject.h"

#include <string>

namespace vkscene {

class GltfModelObject final : public RenderObject {
public:
    explicit GltfModelObject(std::string path, uint32_t textureSlot = 0);

    bool loaded() const { return loaded_; }
    const std::string& error() const { return error_; }

    void buildMesh(std::vector<core::Vertex>& outVertices,
                   std::vector<uint32_t>& outIndices) const override;

private:
    std::string path_{};
    std::vector<core::Vertex> vertices_{};
    std::vector<uint32_t> indices_{};
    bool loaded_ = false;
    std::string error_{};
};

} // namespace vkscene
