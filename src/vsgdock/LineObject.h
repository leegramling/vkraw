#pragma once

#include <vsg/core/ref_ptr.h>

namespace vsg
{
class Node;
}

namespace vkvsg
{

vsg::ref_ptr<vsg::Node> createEquatorLineNode(double equatorialRadiusFeet);

} // namespace vkvsg

