#pragma once

#include "core/EcsWorld.h"
#include "core/SceneGraph.h"
#include "vkscene/RenderObject.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vkscene {

class Scene {
public:
    core::SceneNodeId rootNode() const { return sceneGraph_.root(); }

    core::SceneNodeId addObject(const RenderObjectPtr& object,
                                 const std::string& nodeName,
                                 core::SceneNodeId parent = 0)
    {
        if (!object) return 0;

        const core::EntityId entity = ecs_.createEntity();
        ecs_.setTransform(entity, core::TransformComponent{object->modelMatrix()});
        ecs_.setVisibility(entity, core::VisibilityComponent{true});

        const core::SceneNodeId node = sceneGraph_.createNode(nodeName, parent, entity);
        objects_[node] = object;
        return node;
    }

    RenderObjectPtr object(core::SceneNodeId node) const
    {
        auto it = objects_.find(node);
        if (it == objects_.end()) return {};
        return it->second;
    }

    std::vector<core::SceneNodeId> objectNodes() const
    {
        std::vector<core::SceneNodeId> nodes;
        nodes.reserve(objects_.size());
        for (const auto& [node, object] : objects_) {
            (void)object;
            nodes.push_back(node);
        }
        return nodes;
    }

    void update(float deltaSeconds, float elapsedSeconds)
    {
        for (auto& [node, object] : objects_) {
            (void)node;
            if (object) object->update(deltaSeconds, elapsedSeconds);
        }
        sceneGraph_.updateWorldTransforms();
    }

    core::SceneGraph& graph() { return sceneGraph_; }
    const core::SceneGraph& graph() const { return sceneGraph_; }

    core::EcsWorld& ecs() { return ecs_; }
    const core::EcsWorld& ecs() const { return ecs_; }

private:
    core::SceneGraph sceneGraph_{};
    core::EcsWorld ecs_{};
    std::unordered_map<core::SceneNodeId, RenderObjectPtr> objects_{};
};

} // namespace vkscene
