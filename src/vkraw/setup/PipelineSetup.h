#pragma once

#include <vector>

#include "vkraw/VkContext.h"

namespace vkraw::setup {

void createDescriptorSetLayout(VkContext& context);
void createGraphicsPipeline(VkContext& context, const std::vector<char>& vertShaderCode, const std::vector<char>& fragShaderCode, size_t pushConstantSize,
                            VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VkPipeline* outPipeline = nullptr,
                            bool createPipelineLayout = true);

} // namespace vkraw::setup
