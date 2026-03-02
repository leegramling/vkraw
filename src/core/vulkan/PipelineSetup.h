#pragma once

#include <vector>

#include "core/runtime/VkContext.h"

namespace core::vulkan {

void createDescriptorSetLayout(core::runtime::VkContext& context);
void createGraphicsPipeline(core::runtime::VkContext& context, const std::vector<char>& vertShaderCode, const std::vector<char>& fragShaderCode,
                            size_t pushConstantSize, VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                            VkPipeline* outPipeline = nullptr, bool createPipelineLayout = true);

} // namespace core::vulkan
