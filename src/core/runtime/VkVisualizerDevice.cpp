#include "core/runtime/VkVisualizerApp.h"

#include "vkraw/CubeRenderTypes.h"
#include "core/vulkan/FramebufferSetup.h"
#include "core/vulkan/PipelineSetup.h"
#include "core/vulkan/RenderPassSetup.h"
#include "core/vulkan/SwapchainSetup.h"

#include <VkBootstrap.h>

#include <string>
#include <stdexcept>

namespace vkraw {

std::string VkVisualizerApp::makeScenePipelineKey(vkscene::PrimitiveType primitive, const std::string& vertShader, const std::string& fragShader)
{
    const char* prim = (primitive == vkscene::PrimitiveType::Lines) ? "lines" : "triangles";
    return std::string(prim) + "|" + vertShader + "|" + fragShader;
}

VkPipeline VkVisualizerApp::getOrCreateScenePipeline(vkscene::PrimitiveType primitive, const std::string& vertShader, const std::string& fragShader)
{
    const std::string key = makeScenePipelineKey(primitive, vertShader, fragShader);
    auto it = scenePipelineCache_.find(key);
    if (it != scenePipelineCache_.end()) return it->second;

    const auto vertShaderCode = readShaderFile(vertShader);
    const auto fragShaderCode = readShaderFile(fragShader);
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkPrimitiveTopology topology = (primitive == vkscene::PrimitiveType::Lines) ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    core::vulkan::createGraphicsPipeline(context_, vertShaderCode, fragShaderCode, sizeof(PushConstantData), topology, &pipeline, false);
    scenePipelineCache_.emplace(key, pipeline);
    return pipeline;
}

void VkVisualizerApp::createInstance() {
    vkb::InstanceBuilder builder;
    auto instanceRet = builder.set_app_name("vkRaw")
                           .request_validation_layers()
                           .use_default_debug_messenger()
                           .require_api_version(1, 2, 0)
                           .build();
    if (!instanceRet) {
        throw std::runtime_error(instanceRet.error().message());
    }
    context_.instance = instanceRet.value();
}

void VkVisualizerApp::createSurface() {
    if (glfwCreateWindowSurface(context_.instance.instance, context_.window, nullptr, &context_.surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create Vulkan surface");
    }
}

void VkVisualizerApp::pickPhysicalDevice() {
    auto physRet = vkb::PhysicalDeviceSelector(context_.instance).set_surface(context_.surface).select();
    if (!physRet) {
        throw std::runtime_error(physRet.error().message());
    }
    context_.physicalDevice = physRet.value();

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context_.physicalDevice.physical_device, &properties);
    context_.gpuTimestampsSupported = properties.limits.timestampComputeAndGraphics == VK_TRUE;
    context_.timestampPeriodNs = static_cast<double>(properties.limits.timestampPeriod);
}

void VkVisualizerApp::createDevice() {
    auto deviceRet = vkb::DeviceBuilder(context_.physicalDevice).build();
    if (!deviceRet) {
        throw std::runtime_error(deviceRet.error().message());
    }
    context_.device = deviceRet.value();

    auto graphicsQueueRet = context_.device.get_queue(vkb::QueueType::graphics);
    auto presentQueueRet = context_.device.get_queue(vkb::QueueType::present);
    auto graphicsQueueIndexRet = context_.device.get_queue_index(vkb::QueueType::graphics);

    if (!graphicsQueueRet || !presentQueueRet || !graphicsQueueIndexRet) {
        throw std::runtime_error("failed to get graphics/present queue");
    }

    context_.graphicsQueue = graphicsQueueRet.value();
    context_.presentQueue = presentQueueRet.value();
    context_.graphicsQueueFamily = graphicsQueueIndexRet.value();
}

void VkVisualizerApp::createSwapchain() {
    core::vulkan::createSwapchain(context_, context_.window);
}

void VkVisualizerApp::createRenderPass() {
    core::vulkan::createRenderPass(context_, findDepthFormat());
}

void VkVisualizerApp::createDescriptorSetLayout() {
    core::vulkan::createDescriptorSetLayout(context_);
}

void VkVisualizerApp::createGraphicsPipeline() {
    const auto vertShaderCode = readShaderFile("cube.vert.spv");
    const auto fragShaderCode = readShaderFile("cube.frag.spv");
    core::vulkan::createGraphicsPipeline(context_, vertShaderCode, fragShaderCode, sizeof(PushConstantData), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                         &context_.pipeline, true);
    scenePipelineCache_.clear();
}

void VkVisualizerApp::createFramebuffers() {
    core::vulkan::createFramebuffers(context_);
}

void VkVisualizerApp::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context_.graphicsQueueFamily;

    if (vkCreateCommandPool(context_.device.device, &poolInfo, nullptr, &context_.commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool");
    }
}

} // namespace vkraw
