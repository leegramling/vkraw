#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace vkraw {

using EntityId = uint32_t;

struct TransformComponent {
    glm::mat4 localTransform{1.0f};
};

struct VisibilityComponent {
    bool visible = true;
};

struct MeshComponent {
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
};

class EcsWorld {
public:
    EntityId createEntity()
    {
        return nextEntityId_++;
    }

    void destroyEntity(EntityId id)
    {
        transforms_.erase(id);
        visibility_.erase(id);
        meshes_.erase(id);
    }

    void setTransform(EntityId id, const TransformComponent& t) { transforms_[id] = t; }
    void setVisibility(EntityId id, const VisibilityComponent& v) { visibility_[id] = v; }
    void setMesh(EntityId id, const MeshComponent& m) { meshes_[id] = m; }

    TransformComponent* transform(EntityId id)
    {
        auto it = transforms_.find(id);
        return (it != transforms_.end()) ? &it->second : nullptr;
    }

    VisibilityComponent* visibility(EntityId id)
    {
        auto it = visibility_.find(id);
        return (it != visibility_.end()) ? &it->second : nullptr;
    }

    MeshComponent* mesh(EntityId id)
    {
        auto it = meshes_.find(id);
        return (it != meshes_.end()) ? &it->second : nullptr;
    }

    size_t entityCount() const
    {
        return std::max({transforms_.size(), visibility_.size(), meshes_.size()});
    }

    size_t visibleCount() const
    {
        size_t count = 0;
        for (const auto& kv : visibility_)
        {
            const auto& vis = kv.second;
            if (vis.visible) ++count;
        }
        return count;
    }

private:
    EntityId nextEntityId_ = 1;
    std::unordered_map<EntityId, TransformComponent> transforms_{};
    std::unordered_map<EntityId, VisibilityComponent> visibility_{};
    std::unordered_map<EntityId, MeshComponent> meshes_{};
};

} // namespace vkraw
