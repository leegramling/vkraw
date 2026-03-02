#pragma once

#include "core/runtime/VkContext.h"

namespace core::vulkan {

void createRenderPass(vkraw::VkContext& context, VkFormat depthFormat);

} // namespace core::vulkan
