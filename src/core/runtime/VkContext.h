#pragma once

#include <array>
#include <vector>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <VkBootstrap.h>

namespace core::runtime {

constexpr int kMaxFramesInFlight = 1;

struct VkContext {
    struct TextureResource {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    GLFWwindow* window = nullptr;

    vkb::Instance instance{};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vkb::PhysicalDevice physicalDevice{};
    vkb::Device device{};
    vkb::Swapchain swapchain{};

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = 0;

    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> swapchainFramebuffers;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    std::vector<TextureResource> bindlessTextures{};

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uniformBufferMemory = VK_NULL_HANDLE;
    VkBuffer objectUniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory objectUniformBufferMemory = VK_NULL_HANDLE;
    VkDeviceSize objectUniformStride = 0;
    uint32_t objectUniformCapacity = 0;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;

    std::array<VkSemaphore, kMaxFramesInFlight> imageAvailableSemaphores{};
    std::array<VkSemaphore, kMaxFramesInFlight> renderFinishedSemaphores{};
    std::array<VkFence, kMaxFramesInFlight> inFlightFences{};

    size_t currentFrame = 0;
    bool framebufferResized = false;
    VkPresentModeKHR selectedPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkQueryPool gpuTimestampQueryPool = VK_NULL_HANDLE;
    bool gpuTimestampsSupported = false;
    double timestampPeriodNs = 0.0;
    std::array<bool, kMaxFramesInFlight> gpuQueryValid{};
};

} // namespace core::runtime
