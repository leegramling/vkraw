#include "vkraw/VkVisualizerApp.h"
#include "vkraw/CubeRenderTypes.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace vkraw {

std::vector<char> VkVisualizerApp::readFile(const std::string& filename) {
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

std::vector<char> VkVisualizerApp::readShaderFile(const std::string& filename) {
#ifdef VKRAW_SHADER_DIR
    try {
        return readFile(std::string(VKRAW_SHADER_DIR) + "/" + filename);
    } catch (const std::exception&) {
        // Fall through to relative lookup so local overrides still work.
    }
#endif
    return readFile("shaders/" + filename);
}

const char* VkVisualizerApp::presentModeToString(VkPresentModeKHR mode) {
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

void VkVisualizerApp::framebufferResizeCallback(GLFWwindow* window, int, int) {
    auto* app = reinterpret_cast<VkVisualizerApp*>(glfwGetWindowUserPointer(window));
    app->context_.framebufferResized = true;
}

void VkVisualizerApp::run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
}

void VkVisualizerApp::initWindow() {
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

void VkVisualizerApp::initVulkan() {
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
    cube_.rebuildOffsets();
    rebuildSceneMesh();
    createVertexBuffer();
    createIndexBuffer();
    createUniformBuffer();
    createDescriptorPool();
    createDescriptorSet();
    createCommandBuffers();
    createTimestampQueryPool();
    createSyncObjects();
    initImGui();

    std::cout << "[START] vkraw cubes=" << cube_.cubeCount
              << " present_mode=" << presentModeToString(context_.selectedPresentMode)
              << " timestamps=" << (context_.gpuTimestampQueryPool != VK_NULL_HANDLE ? "on" : "off")
              << std::endl;
}

void VkVisualizerApp::recreateSwapchain() {
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

void VkVisualizerApp::mainLoop() {
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

void VkVisualizerApp::cleanupSwapchain() {
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

void VkVisualizerApp::cleanup() {
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

int runVkrawApp()
{
    try {
        VkVisualizerApp visualizer;
        visualizer.run();
    } catch (const std::exception& e) {
        std::cout << "[EXIT] vkraw status=FAIL code=1 reason=\"" << e.what() << "\"" << std::endl;
        std::cerr << "error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

} // namespace vkraw
