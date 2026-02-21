#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace vkraw {

using SceneNodeId = uint32_t;

struct SceneNode {
    std::string name;
    SceneNodeId parent = 0;
    std::vector<SceneNodeId> children;
    glm::mat4 localTransform{1.0f};
    glm::mat4 worldTransform{1.0f};
    bool visible = true;
    uint32_t entity = 0;
};

class SceneGraph {
public:
    SceneGraph()
    {
        nodes_.push_back(SceneNode{"Root", 0, {}, glm::mat4(1.0f), glm::mat4(1.0f), true, 0});
    }

    SceneNodeId root() const { return 0; }

    SceneNodeId createNode(const std::string& name, SceneNodeId parent, uint32_t entity)
    {
        SceneNodeId id = static_cast<SceneNodeId>(nodes_.size());
        nodes_.push_back(SceneNode{name, parent, {}, glm::mat4(1.0f), glm::mat4(1.0f), true, entity});
        nodes_[parent].children.push_back(id);
        return id;
    }

    SceneNode* find(SceneNodeId id)
    {
        if (id >= nodes_.size()) return nullptr;
        return &nodes_[id];
    }

    const SceneNode* find(SceneNodeId id) const
    {
        if (id >= nodes_.size()) return nullptr;
        return &nodes_[id];
    }

    void updateWorldTransforms()
    {
        if (nodes_.empty()) return;
        updateWorldRecursive(0, glm::mat4(1.0f));
    }

    size_t nodeCount() const { return nodes_.size(); }

    size_t visibleNodeCount() const
    {
        size_t count = 0;
        for (const auto& n : nodes_)
        {
            if (n.visible) ++count;
        }
        return count;
    }

private:
    void updateWorldRecursive(SceneNodeId id, const glm::mat4& parent)
    {
        SceneNode& node = nodes_[id];
        node.worldTransform = parent * node.localTransform;
        for (SceneNodeId child : node.children)
        {
            updateWorldRecursive(child, node.worldTransform);
        }
    }

    std::vector<SceneNode> nodes_{};
};

} // namespace vkraw
