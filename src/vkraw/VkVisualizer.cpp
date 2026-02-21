#include "vkraw/VkVisualizer.h"
#include "vkraw/CubeObject.h"
#include "vkraw/CubeRenderTypes.h"
#include "vkraw/UIObject.h"
#include "vkraw/VkContext.h"
#include "vkraw/setup/FramebufferSetup.h"
#include "vkraw/setup/PipelineSetup.h"
#include "vkraw/setup/RenderPassSetup.h"
#include "vkraw/setup/SwapchainSetup.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <VkBootstrap.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

namespace {

constexpr uint32_t kWindowWidth = 1280;
constexpr uint32_t kWindowHeight = 720;
struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;

    static VkVertexInputBindingDescription bindingDescription() {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 2> attributes{};
        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(Vertex, pos);

        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(Vertex, color);
        return attributes;
    }
};

struct UniformBufferObject {
    glm::mat4 viewProj;
};

struct PushConstantData {
    glm::mat4 model;
};

const std::vector<Vertex> kVertices = {
    {{-1.0f, -1.0f, -1.0f}, {1.0f, 0.2f, 0.2f}},
    {{1.0f, -1.0f, -1.0f}, {0.2f, 1.0f, 0.2f}},
    {{1.0f, 1.0f, -1.0f}, {0.2f, 0.2f, 1.0f}},
    {{-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, 0.2f}},
    {{-1.0f, -1.0f, 1.0f}, {1.0f, 0.2f, 1.0f}},
    {{1.0f, -1.0f, 1.0f}, {0.2f, 1.0f, 1.0f}},
    {{1.0f, 1.0f, 1.0f}, {0.9f, 0.9f, 0.9f}},
    {{-1.0f, 1.0f, 1.0f}, {0.5f, 0.5f, 0.9f}},
};

const std::vector<uint16_t> kIndices = {
    0, 1, 2, 2, 3, 0,
    4, 5, 6, 6, 7, 4,
    0, 4, 7, 7, 3, 0,
    1, 5, 6, 6, 2, 1,
    3, 2, 6, 6, 7, 3,
    0, 1, 5, 5, 4, 0,
};

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }

    const auto size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    file.close();
    return buffer;
}

std::vector<char> readShaderFile(const std::string& filename) {
#ifdef VKRAW_SHADER_DIR
    try {
        return readFile(std::string(VKRAW_SHADER_DIR) + "/" + filename);
    } catch (const std::exception&) {
        // Fall through to relative lookup so local overrides still work.
    }
#endif
    return readFile("shaders/" + filename);
}

const char* presentModeToString(VkPresentModeKHR mode) {
    switch (mode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:
            return "IMMEDIATE";
        case VK_PRESENT_MODE_MAILBOX_KHR:
            return "MAILBOX";
        case VK_PRESENT_MODE_FIFO_KHR:
            return "FIFO";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
            return "FIFO_RELAXED";
        default:
            return "OTHER";
    }
}

using vkraw::VkContext;
using vkraw::kMaxFramesInFlight;

class VkVisualizer {
  public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

  private:
    VkContext context_{};

    vkraw::CubeObject cube_;
    vkraw::UIObject ui_;
    float gpuFrameMs_ = 0.0f;
    uint64_t frameCount_ = 0;
    float runSeconds_ = 0.0f;
    float cpuFrameMs_ = 0.0f;

    static void framebufferResizeCallback(GLFWwindow* window, int, int) {
        auto* app = reinterpret_cast<VkVisualizer*>(glfwGetWindowUserPointer(window));
        app->context_.framebufferResized = true;
    }

    void initWindow() {
        if (!glfwInit()) {
            throw std::runtime_error("failed to initialize GLFW");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        context_.window = glfwCreateWindow(static_cast<int>(kWindowWidth), static_cast<int>(kWindowHeight), "vkRaw - vk-bootstrap", nullptr,
                                   nullptr);
        if (!context_.window) {
            throw std::runtime_error("failed to create GLFW window");
        }

        glfwSetWindowUserPointer(context_.window, this);
        glfwSetFramebufferSizeCallback(context_.window, framebufferResizeCallback);
    }

    void initVulkan() {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createDevice();
        createSwapchain();
        createRenderPass();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createCommandPool();
        createDepthResources();
        createFramebuffers();
        createVertexBuffer();
        createIndexBuffer();
        createUniformBuffer();
        createDescriptorPool();
        createDescriptorSet();
        createCommandBuffers();
        createTimestampQueryPool();
        createSyncObjects();
        cube_.rebuildOffsets();
        initImGui();

        std::cout << "[START] vkraw cubes=" << cube_.cubeCount
                  << " present_mode=" << presentModeToString(context_.selectedPresentMode)
                  << " timestamps=" << (context_.gpuTimestampQueryPool != VK_NULL_HANDLE ? "on" : "off")
                  << std::endl;
    }

    void createInstance() {
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

    void createSurface() {
        if (glfwCreateWindowSurface(context_.instance.instance, context_.window, nullptr, &context_.surface) != VK_SUCCESS) {
            throw std::runtime_error("failed to create Vulkan surface");
        }
    }

    void pickPhysicalDevice() {
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

    void createDevice() {
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

    void createSwapchain() {
        vkraw::setup::createSwapchain(context_, context_.window);
    }

    void createRenderPass() {
        vkraw::setup::createRenderPass(context_, findDepthFormat());
    }

    void createDescriptorSetLayout() {
        vkraw::setup::createDescriptorSetLayout(context_);
    }

    void createGraphicsPipeline() {
        const auto vertShaderCode = readShaderFile("cube.vert.spv");
        const auto fragShaderCode = readShaderFile("cube.frag.spv");
        vkraw::setup::createGraphicsPipeline(context_, vertShaderCode, fragShaderCode, sizeof(PushConstantData));
    }

    void createFramebuffers() {
        vkraw::setup::createFramebuffers(context_);
    }

    void createCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = context_.graphicsQueueFamily;

        if (vkCreateCommandPool(context_.device.device, &poolInfo, nullptr, &context_.commandPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create command pool");
        }
    }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties{};
        vkGetPhysicalDeviceMemoryProperties(context_.physicalDevice.physical_device, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1U << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw std::runtime_error("failed to find suitable memory type");
    }

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer,
                      VkDeviceMemory& bufferMemory) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(context_.device.device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create buffer");
        }

        VkMemoryRequirements memRequirements{};
        vkGetBufferMemoryRequirements(context_.device.device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(context_.device.device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate buffer memory");
        }

        vkBindBufferMemory(context_.device.device, buffer, bufferMemory, 0);
    }

    void uploadToMemory(VkDeviceMemory memory, const void* src, VkDeviceSize size) {
        void* data = nullptr;
        vkMapMemory(context_.device.device, memory, 0, size, 0, &data);
        std::memcpy(data, src, static_cast<size_t>(size));
        vkUnmapMemory(context_.device.device, memory);
    }

    void createVertexBuffer() {
        const VkDeviceSize bufferSize = sizeof(kVertices[0]) * kVertices.size();
        createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, context_.vertexBuffer, context_.vertexBufferMemory);
        uploadToMemory(context_.vertexBufferMemory, kVertices.data(), bufferSize);
    }

    void createIndexBuffer() {
        const VkDeviceSize bufferSize = sizeof(kIndices[0]) * kIndices.size();
        createBuffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, context_.indexBuffer, context_.indexBufferMemory);
        uploadToMemory(context_.indexBufferMemory, kIndices.data(), bufferSize);
    }

    void createUniformBuffer() {
        createBuffer(sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, context_.uniformBuffer, context_.uniformBufferMemory);
    }

    void createDescriptorPool() {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;

        if (vkCreateDescriptorPool(context_.device.device, &poolInfo, nullptr, &context_.descriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool");
        }
    }

    void createDescriptorSet() {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = context_.descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &context_.descriptorSetLayout;

        if (vkAllocateDescriptorSets(context_.device.device, &allocInfo, &context_.descriptorSet) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate descriptor set");
        }

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = context_.uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = context_.descriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(context_.device.device, 1, &descriptorWrite, 0, nullptr);
    }

    void createCommandBuffers() {
        context_.commandBuffers.resize(context_.swapchainImages.size());

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = context_.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(context_.commandBuffers.size());

        if (vkAllocateCommandBuffers(context_.device.device, &allocInfo, context_.commandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers");
        }
    }

    void createTimestampQueryPool() {
        if (!context_.gpuTimestampsSupported) {
            return;
        }

        if (context_.gpuTimestampQueryPool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(context_.device.device, context_.gpuTimestampQueryPool, nullptr);
            context_.gpuTimestampQueryPool = VK_NULL_HANDLE;
        }

        VkQueryPoolCreateInfo queryPoolInfo{};
        queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryPoolInfo.queryCount = 2U * static_cast<uint32_t>(kMaxFramesInFlight);

        if (vkCreateQueryPool(context_.device.device, &queryPoolInfo, nullptr, &context_.gpuTimestampQueryPool) != VK_SUCCESS) {
            context_.gpuTimestampsSupported = false;
            return;
        }

        context_.gpuQueryValid.fill(false);
    }

    void createSyncObjects() {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < kMaxFramesInFlight; i++) {
            if (vkCreateSemaphore(context_.device.device, &semaphoreInfo, nullptr, &context_.imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(context_.device.device, &semaphoreInfo, nullptr, &context_.renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(context_.device.device, &fenceInfo, nullptr, &context_.inFlightFences[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create sync objects");
            }
        }
    }

    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
        for (VkFormat format : candidates) {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(context_.physicalDevice.physical_device, format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
                return format;
            }
            if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
                return format;
            }
        }
        throw std::runtime_error("failed to find supported format");
    }

    VkFormat findDepthFormat() {
        context_.depthFormat = findSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                                           VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        return context_.depthFormat;
    }

    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                     VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = tiling;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(context_.device.device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image");
        }

        VkMemoryRequirements memRequirements{};
        vkGetImageMemoryRequirements(context_.device.device, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(context_.device.device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate image memory");
        }

        vkBindImageMemory(context_.device.device, image, imageMemory, 0);
    }

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView = VK_NULL_HANDLE;
        if (vkCreateImageView(context_.device.device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image view");
        }
        return imageView;
    }

    void createDepthResources() {
        createImage(context_.swapchain.extent.width, context_.swapchain.extent.height, findDepthFormat(), VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, context_.depthImage, context_.depthImageMemory);
        context_.depthImageView = createImageView(context_.depthImage, context_.depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    void initImGui() {
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

        if (!ImGui_ImplGlfw_InitForVulkan(context_.window, true)) {
            throw std::runtime_error("failed to initialize imgui glfw backend");
        }

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_2;
        initInfo.Instance = context_.instance.instance;
        initInfo.PhysicalDevice = context_.physicalDevice.physical_device;
        initInfo.Device = context_.device.device;
        initInfo.QueueFamily = context_.graphicsQueueFamily;
        initInfo.Queue = context_.graphicsQueue;
        initInfo.DescriptorPool = context_.imguiDescriptorPool;
        initInfo.MinImageCount = context_.swapchain.image_count;
        initInfo.ImageCount = context_.swapchain.image_count;
        initInfo.UseDynamicRendering = false;
        initInfo.PipelineInfoMain.RenderPass = context_.renderPass;
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        if (!ImGui_ImplVulkan_Init(&initInfo)) {
            throw std::runtime_error("failed to initialize imgui vulkan backend");
        }
    }

    void processInput(float deltaSeconds) {
        cube_.processInput(context_.window, deltaSeconds);
    }

    void updateUniformBuffer() {
        UniformBufferObject ubo{};

        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 120.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection =
            glm::perspective(glm::radians(60.0f), context_.swapchain.extent.width / static_cast<float>(context_.swapchain.extent.height), 0.1f, 2000.0f);
        projection[1][1] *= -1.0f;

        ubo.viewProj = projection * view;
        uploadToMemory(context_.uniformBufferMemory, &ubo, sizeof(ubo));
    }

    glm::mat4 computeBaseRotation(float elapsedSeconds) const {
        return cube_.computeBaseRotation(elapsedSeconds);
    }

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, float elapsedSeconds, size_t frameIndex) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin command buffer");
        }

        const uint32_t queryStart = 2U * static_cast<uint32_t>(frameIndex);
        if (context_.gpuTimestampQueryPool != VK_NULL_HANDLE) {
            vkCmdResetQueryPool(commandBuffer, context_.gpuTimestampQueryPool, queryStart, 2);
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, context_.gpuTimestampQueryPool, queryStart);
        }

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.04f, 0.05f, 0.08f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = context_.renderPass;
        renderPassInfo.framebuffer = context_.swapchainFramebuffers[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = context_.swapchain.extent;
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context_.pipeline);

        VkBuffer vertexBuffers[] = {context_.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, context_.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context_.pipelineLayout, 0, 1, &context_.descriptorSet, 0, nullptr);

        const glm::mat4 baseRotation = computeBaseRotation(elapsedSeconds);
        for (const glm::vec3& offset : cube_.offsets) {
            PushConstantData push{};
            push.model = glm::translate(baseRotation, offset);
            vkCmdPushConstants(commandBuffer, context_.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantData), &push);
            vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(kIndices.size()), 1, 0, 0, 0);
        }

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

        vkCmdEndRenderPass(commandBuffer);

        if (context_.gpuTimestampQueryPool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, context_.gpuTimestampQueryPool, queryStart + 1);
        }

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer");
        }
    }

    void recreateSwapchain() {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(context_.window, &width, &height);
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(context_.window, &width, &height);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(context_.device.device);

        cleanupSwapchain();

        createSwapchain();
        createRenderPass();
        createDepthResources();
        createGraphicsPipeline();
        createFramebuffers();
        createCommandBuffers();
        createTimestampQueryPool();

        ImGui_ImplVulkan_SetMinImageCount(context_.swapchain.image_count);
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_2;
        initInfo.Instance = context_.instance.instance;
        initInfo.PhysicalDevice = context_.physicalDevice.physical_device;
        initInfo.Device = context_.device.device;
        initInfo.QueueFamily = context_.graphicsQueueFamily;
        initInfo.Queue = context_.graphicsQueue;
        initInfo.DescriptorPool = context_.imguiDescriptorPool;
        initInfo.MinImageCount = context_.swapchain.image_count;
        initInfo.ImageCount = context_.swapchain.image_count;
        initInfo.UseDynamicRendering = false;
        initInfo.PipelineInfoMain.RenderPass = context_.renderPass;
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        if (!ImGui_ImplVulkan_Init(&initInfo)) {
            throw std::runtime_error("failed to reinitialize imgui vulkan backend");
        }
    }

    void drawFrame(float deltaSeconds, float elapsedSeconds) {
        vkWaitForFences(context_.device.device, 1, &context_.inFlightFences[context_.currentFrame], VK_TRUE, UINT64_MAX);

        if (context_.gpuTimestampQueryPool != VK_NULL_HANDLE && context_.gpuQueryValid[context_.currentFrame]) {
            const uint32_t queryStart = 2U * static_cast<uint32_t>(context_.currentFrame);
            uint64_t timestamps[2] = {};
            const VkResult result = vkGetQueryPoolResults(
                context_.device.device,
                context_.gpuTimestampQueryPool,
                queryStart,
                2,
                sizeof(timestamps),
                timestamps,
                sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT);
            if (result == VK_SUCCESS && timestamps[1] >= timestamps[0]) {
                const double deltaTicks = static_cast<double>(timestamps[1] - timestamps[0]);
                gpuFrameMs_ = static_cast<float>((deltaTicks * context_.timestampPeriodNs) * 1e-6);
            }
        }

        uint32_t imageIndex = 0;
        const VkResult acquireResult =
            vkAcquireNextImageKHR(context_.device.device, context_.swapchain.swapchain, UINT64_MAX, context_.imageAvailableSemaphores[context_.currentFrame],
                                  VK_NULL_HANDLE, &imageIndex);

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return;
        }
        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("failed to acquire swapchain image");
        }

        vkResetFences(context_.device.device, 1, &context_.inFlightFences[context_.currentFrame]);
        vkResetCommandBuffer(context_.commandBuffers[imageIndex], 0);

        processInput(deltaSeconds);

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ui_.fps = (deltaSeconds > 0.0f) ? (1.0f / deltaSeconds) : 0.0f;
        ui_.frameTimeMs = 1000.0f * deltaSeconds;
        ui_.gpuFrameMs = gpuFrameMs_;
        if (ui_.draw(cube_, presentModeToString(context_.selectedPresentMode), context_.gpuTimestampQueryPool != VK_NULL_HANDLE)) {
            cube_.rebuildOffsets();
        }

        ImGui::Render();

        updateUniformBuffer();
        recordCommandBuffer(context_.commandBuffers[imageIndex], imageIndex, elapsedSeconds, context_.currentFrame);
        context_.gpuQueryValid[context_.currentFrame] = (context_.gpuTimestampQueryPool != VK_NULL_HANDLE);

        VkSemaphore waitSemaphores[] = {context_.imageAvailableSemaphores[context_.currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphore signalSemaphores[] = {context_.renderFinishedSemaphores[context_.currentFrame]};

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &context_.commandBuffers[imageIndex];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(context_.graphicsQueue, 1, &submitInfo, context_.inFlightFences[context_.currentFrame]) != VK_SUCCESS) {
            throw std::runtime_error("failed to submit draw command buffer");
        }

        VkSwapchainKHR swapchains[] = {context_.swapchain.swapchain};
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &imageIndex;

        const VkResult presentResult = vkQueuePresentKHR(context_.presentQueue, &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || context_.framebufferResized) {
            context_.framebufferResized = false;
            recreateSwapchain();
        } else if (presentResult != VK_SUCCESS) {
            throw std::runtime_error("failed to present swapchain image");
        }

        context_.currentFrame = (context_.currentFrame + 1) % kMaxFramesInFlight;
    }

    void mainLoop() {
        const auto start = std::chrono::high_resolution_clock::now();
        auto last = start;

        while (!glfwWindowShouldClose(context_.window)) {
            glfwPollEvents();

            const auto now = std::chrono::high_resolution_clock::now();
            const float deltaSeconds = std::chrono::duration<float>(now - last).count();
            const float elapsedSeconds = std::chrono::duration<float>(now - start).count();
            last = now;
            ++frameCount_;
            runSeconds_ = elapsedSeconds;
            cpuFrameMs_ = 1000.0f * deltaSeconds;

            drawFrame(deltaSeconds, elapsedSeconds);
        }

        vkDeviceWaitIdle(context_.device.device);
        const uint64_t triangles = cube_.triangles();
        const uint64_t vertices = cube_.vertices();
        std::cout << "[EXIT] vkraw status=OK code=0"
                  << " frames=" << frameCount_
                  << " seconds=" << runSeconds_
                  << " cubes=" << cube_.cubeCount
                  << " triangles=" << triangles
                  << " vertices=" << vertices
                  << " fps=" << ui_.fps
                  << " cpu_ms=" << cpuFrameMs_
                  << " gpu_ms=" << gpuFrameMs_
                  << " present_mode=" << presentModeToString(context_.selectedPresentMode)
                  << std::endl;
    }

    void cleanupSwapchain() {
        for (VkFramebuffer framebuffer : context_.swapchainFramebuffers) {
            vkDestroyFramebuffer(context_.device.device, framebuffer, nullptr);
        }
        context_.swapchainFramebuffers.clear();

        if (!context_.commandBuffers.empty()) {
            vkFreeCommandBuffers(context_.device.device, context_.commandPool, static_cast<uint32_t>(context_.commandBuffers.size()), context_.commandBuffers.data());
            context_.commandBuffers.clear();
        }

        if (context_.gpuTimestampQueryPool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(context_.device.device, context_.gpuTimestampQueryPool, nullptr);
            context_.gpuTimestampQueryPool = VK_NULL_HANDLE;
        }

        if (context_.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(context_.device.device, context_.pipeline, nullptr);
            context_.pipeline = VK_NULL_HANDLE;
        }
        if (context_.pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(context_.device.device, context_.pipelineLayout, nullptr);
            context_.pipelineLayout = VK_NULL_HANDLE;
        }
        if (context_.renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(context_.device.device, context_.renderPass, nullptr);
            context_.renderPass = VK_NULL_HANDLE;
        }

        if (context_.depthImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(context_.device.device, context_.depthImageView, nullptr);
            context_.depthImageView = VK_NULL_HANDLE;
        }
        if (context_.depthImage != VK_NULL_HANDLE) {
            vkDestroyImage(context_.device.device, context_.depthImage, nullptr);
            context_.depthImage = VK_NULL_HANDLE;
        }
        if (context_.depthImageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(context_.device.device, context_.depthImageMemory, nullptr);
            context_.depthImageMemory = VK_NULL_HANDLE;
        }

        if (!context_.swapchainImageViews.empty()) {
            context_.swapchain.destroy_image_views(context_.swapchainImageViews);
            context_.swapchainImageViews.clear();
        }
        if (context_.swapchain.swapchain != VK_NULL_HANDLE) {
            vkb::destroy_swapchain(context_.swapchain);
            context_.swapchain = {};
        }
    }

    void cleanup() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        cleanupSwapchain();

        if (context_.imguiDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(context_.device.device, context_.imguiDescriptorPool, nullptr);
        }
        if (context_.descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(context_.device.device, context_.descriptorPool, nullptr);
        }
        if (context_.descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(context_.device.device, context_.descriptorSetLayout, nullptr);
        }

        if (context_.uniformBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(context_.device.device, context_.uniformBuffer, nullptr);
        }
        if (context_.uniformBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(context_.device.device, context_.uniformBufferMemory, nullptr);
        }
        if (context_.indexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(context_.device.device, context_.indexBuffer, nullptr);
        }
        if (context_.indexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(context_.device.device, context_.indexBufferMemory, nullptr);
        }
        if (context_.vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(context_.device.device, context_.vertexBuffer, nullptr);
        }
        if (context_.vertexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(context_.device.device, context_.vertexBufferMemory, nullptr);
        }

        for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
            if (context_.imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(context_.device.device, context_.imageAvailableSemaphores[i], nullptr);
            }
            if (context_.renderFinishedSemaphores[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(context_.device.device, context_.renderFinishedSemaphores[i], nullptr);
            }
            if (context_.inFlightFences[i] != VK_NULL_HANDLE) {
                vkDestroyFence(context_.device.device, context_.inFlightFences[i], nullptr);
            }
        }

        if (context_.commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(context_.device.device, context_.commandPool, nullptr);
        }

        if (context_.device.device != VK_NULL_HANDLE) {
            vkb::destroy_device(context_.device);
        }
        if (context_.surface != VK_NULL_HANDLE) {
            vkb::destroy_surface(context_.instance, context_.surface);
        }
        if (context_.instance.instance != VK_NULL_HANDLE) {
            vkb::destroy_instance(context_.instance);
        }

        if (context_.window) {
            glfwDestroyWindow(context_.window);
            context_.window = nullptr;
        }
        glfwTerminate();
    }
};

} // namespace

namespace vkraw {

int runVkrawApp()
{
    try {
        VkVisualizer visualizer;
        visualizer.run();
    } catch (const std::exception& e) {
        std::cout << "[EXIT] vkraw status=FAIL code=1 reason=\"" << e.what() << "\"" << std::endl;
        std::cerr << "error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

} // namespace vkraw
