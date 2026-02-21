#pragma once

#include "vkraw/VkContext.h"

namespace vkraw::setup {

void createRenderPass(VkContext& context, VkFormat depthFormat);

} // namespace vkraw::setup
