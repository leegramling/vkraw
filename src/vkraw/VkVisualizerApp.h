#pragma once

#include "vkraw/CubeRenderTypes.h"
#include "vkraw/EcsWorld.h"
#include "vkraw/GlobeObject.h"
#include "vkraw/SceneGraph.h"
#include "vkraw/UIObject.h"
#include "vkraw/VkContext.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace vkraw {

class VkVisualizerApp {
public:
    void run();
    void setRunDurationSeconds(float seconds) { runDurationSeconds_ = seconds; }
    void setEarthTexturePath(std::string path) { earthTexturePath_ = std::move(path); }

private:
    static constexpr uint32_t kWindowWidth = 1280;
    static constexpr uint32_t kWindowHeight = 720;

    VkContext context_{};

    GlobeObject globe_{};
    SceneGraph sceneGraph_{};
    EcsWorld ecs_{};
    SceneNodeId globeSceneNode_ = 0;
    EntityId globeEntity_ = 0;
    UIObject ui_{};
    float gpuFrameMs_ = 0.0f;
    uint64_t frameCount_ = 0;
    float runSeconds_ = 0.0f;
    float cpuFrameMs_ = 0.0f;
    float runDurationSeconds_ = 0.0f;
    std::string earthTexturePath_{};
    bool textureLoadedFromFile_ = false;
    std::string textureSourceLabel_ = "procedural";
    std::vector<Vertex> sceneVertices_{};
    std::vector<uint32_t> sceneIndices_{};
    uint32_t sceneIndexCount_ = 0;

    static void framebufferResizeCallback(GLFWwindow* window, int, int);

    static std::vector<char> readFile(const std::string& filename);
    static std::vector<char> readShaderFile(const std::string& filename);
    static const char* presentModeToString(VkPresentModeKHR mode);

    void initWindow();
    void initVulkan();

    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createDevice();
    void createSwapchain();
    void createRenderPass();
    void createDescriptorSetLayout();
    void createGraphicsPipeline();
    void createFramebuffers();
    void createCommandPool();

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void uploadToMemory(VkDeviceMemory memory, const void* src, VkDeviceSize size);
    void createVertexBuffer();
    void createIndexBuffer();
    void createUniformBuffer();
    void createTextureResources();
    void createDescriptorPool();
    void createDescriptorSet();
    void createCommandBuffers();
    void createTimestampQueryPool();
    void createSyncObjects();
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    VkFormat findDepthFormat();
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    void createDepthResources();
    void destroyTextureResources();
    void rebuildSceneMesh();
    void rebuildGpuMeshBuffers();
    void initSceneSystems();

    void initImGui();

    void processInput(float deltaSeconds);
    void updateUniformBuffer();
    glm::mat4 computeBaseRotation(float elapsedSeconds) const;
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, float elapsedSeconds, size_t frameIndex);
    void recreateSwapchain();
    void drawFrame(float deltaSeconds, float elapsedSeconds);
    void mainLoop();

    void cleanupSwapchain();
    void cleanup();
};

int runVkrawApp(int argc, char** argv);

} // namespace vkraw
