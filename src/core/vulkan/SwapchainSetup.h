#pragma once

#include "core/runtime/VkContext.h"

namespace core::vulkan {

void createSwapchain(vkraw::VkContext& context, GLFWwindow* window);

} // namespace core::vulkan
