#include "core/runtime/VkVisualizerApp.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <array>
#include <stdexcept>

namespace core::runtime {

namespace {

template<class T>
requires requires(T info) {
    info.RenderPass = VK_NULL_HANDLE;
    info.Subpass = 0;
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
}
void configureImGuiVulkanPipelineInfo(T& initInfo, VkRenderPass renderPass)
{
    initInfo.RenderPass = renderPass;
    initInfo.Subpass = 0;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
}

template<class T>
requires requires(T info) {
    info.PipelineInfoMain.RenderPass = VK_NULL_HANDLE;
    info.PipelineInfoMain.Subpass = 0;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
}
void configureImGuiVulkanPipelineInfo(T& initInfo, VkRenderPass renderPass)
{
    initInfo.PipelineInfoMain.RenderPass = renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
}

} // namespace

void VkVisualizerApp::initImGui() {
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(context_.device.device, &poolInfo, nullptr, &context_.imguiDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create imgui descriptor pool");
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    if (!ImGui_ImplGlfw_InitForVulkan(context_.window, true)) {
        throw std::runtime_error("failed to initialize imgui glfw backend");
    }

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = context_.instance.instance;
    initInfo.PhysicalDevice = context_.physicalDevice.physical_device;
    initInfo.Device = context_.device.device;
    initInfo.QueueFamily = context_.graphicsQueueFamily;
    initInfo.Queue = context_.graphicsQueue;
    initInfo.DescriptorPool = context_.imguiDescriptorPool;
    initInfo.MinImageCount = context_.swapchain.image_count;
    initInfo.ImageCount = context_.swapchain.image_count;
    initInfo.UseDynamicRendering = false;
    configureImGuiVulkanPipelineInfo(initInfo, context_.renderPass);

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("failed to initialize imgui vulkan backend");
    }
}

} // namespace core::runtime
