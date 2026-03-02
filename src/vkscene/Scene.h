#pragma once

#include "vkraw/EcsWorld.h"
#include "vkraw/SceneGraph.h"
#include "vkscene/RenderObject.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vkscene {

class Scene {
public:
    vkraw::SceneNodeId rootNode() const { return sceneGraph_.root(); }

    vkraw::SceneNodeId addObject(const RenderObjectPtr& object,
                                 const std::string& nodeName,
                                 vkraw::SceneNodeId parent = 0)
    {
        if (!object) return 0;

        const vkraw::EntityId entity = ecs_.createEntity();
        ecs_.setTransform(entity, vkraw::TransformComponent{object->modelMatrix()});
        ecs_.setVisibility(entity, vkraw::VisibilityComponent{true});

        const vkraw::SceneNodeId node = sceneGraph_.createNode(nodeName, parent, entity);
        objects_[node] = object;
        return node;
    }

    RenderObjectPtr object(vkraw::SceneNodeId node) const
    {
        auto it = objects_.find(node);
        if (it == objects_.end()) return {};
        return it->second;
    }

    std::vector<vkraw::SceneNodeId> objectNodes() const
    {
        std::vector<vkraw::SceneNodeId> nodes;
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

    vkraw::SceneGraph& graph() { return sceneGraph_; }
    const vkraw::SceneGraph& graph() const { return sceneGraph_; }

    vkraw::EcsWorld& ecs() { return ecs_; }
    const vkraw::EcsWorld& ecs() const { return ecs_; }

private:
    vkraw::SceneGraph sceneGraph_{};
    vkraw::EcsWorld ecs_{};
    std::unordered_map<vkraw::SceneNodeId, RenderObjectPtr> objects_{};
};

} // namespace vkscene
