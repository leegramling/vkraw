#pragma once

#include <vsg/core/ref_ptr.h>

#include <string>

namespace vsg
{
class Node;
}

namespace vkvsg
{

vsg::ref_ptr<vsg::Node> createGlobeNode(const std::string& texturePath,
                                        bool wireframe,
                                        double equatorialRadiusFeet,
                                        double polarRadiusFeet,
                                        bool& loadedFromFile);

} // namespace vkvsg

