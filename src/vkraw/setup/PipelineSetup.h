#pragma once

#include <vector>

#include "vkraw/VkContext.h"

namespace vkraw::setup {

void createDescriptorSetLayout(VkContext& context);
void createGraphicsPipeline(VkContext& context, const std::vector<char>& vertShaderCode, const std::vector<char>& fragShaderCode, size_t pushConstantSize);

} // namespace vkraw::setup
