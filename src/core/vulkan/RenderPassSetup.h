#pragma once

#include "core/runtime/VkContext.h"

namespace core::vulkan {

void createRenderPass(core::runtime::VkContext& context, VkFormat depthFormat);

} // namespace core::vulkan
