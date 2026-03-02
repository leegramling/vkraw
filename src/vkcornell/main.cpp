#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "enginecore/Camera.hpp"
#include "enginecore/Model.hpp"
#include "vulkancore/Buffer.hpp"
#include "vulkancore/CommandQueueManager.hpp"
#include "vulkancore/Context.hpp"
#include "vulkancore/Framebuffer.hpp"
#include "vulkancore/Pipeline.hpp"
#include "vulkancore/RenderPass.hpp"
#include "vulkancore/Texture.hpp"

namespace {

struct CameraUBO {
  glm::mat4 mvp{1.0f};
};

bool initWindow(GLFWwindow** outWindow) {
  if (!glfwInit()) return false;

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  GLFWwindow* window = glfwCreateWindow(1280, 720, "vkcornell", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return false;
  }

  glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int) {
    if (key == GLFW_KEY_ESCAPE && action != GLFW_RELEASE) glfwSetWindowShouldClose(w, GLFW_TRUE);
  });

  if (outWindow) *outWindow = window;
  return true;
}

void addQuad(EngineCore::Mesh& mesh,
             const glm::vec3& a,
             const glm::vec3& b,
             const glm::vec3& c,
             const glm::vec3& d,
             const glm::vec3& color) {
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());

  EngineCore::Vertex va{};
  va.pos = a;
  va.normal = color;
  mesh.vertices.push_back(va);

  EngineCore::Vertex vb{};
  vb.pos = b;
  vb.normal = color;
  mesh.vertices.push_back(vb);

  EngineCore::Vertex vc{};
  vc.pos = c;
  vc.normal = color;
  mesh.vertices.push_back(vc);

  EngineCore::Vertex vd{};
  vd.pos = d;
  vd.normal = color;
  mesh.vertices.push_back(vd);

  mesh.indices.push_back(base + 0);
  mesh.indices.push_back(base + 1);
  mesh.indices.push_back(base + 2);
  mesh.indices.push_back(base + 0);
  mesh.indices.push_back(base + 2);
  mesh.indices.push_back(base + 3);
}

void addBox(EngineCore::Mesh& mesh,
            const glm::vec3& minP,
            const glm::vec3& maxP,
            const glm::vec3& color) {
  const glm::vec3 p000(minP.x, minP.y, minP.z);
  const glm::vec3 p001(minP.x, minP.y, maxP.z);
  const glm::vec3 p010(minP.x, maxP.y, minP.z);
  const glm::vec3 p011(minP.x, maxP.y, maxP.z);
  const glm::vec3 p100(maxP.x, minP.y, minP.z);
  const glm::vec3 p101(maxP.x, minP.y, maxP.z);
  const glm::vec3 p110(maxP.x, maxP.y, minP.z);
  const glm::vec3 p111(maxP.x, maxP.y, maxP.z);

  addQuad(mesh, p001, p101, p111, p011, color);
  addQuad(mesh, p100, p000, p010, p110, color);
  addQuad(mesh, p000, p001, p011, p010, color);
  addQuad(mesh, p101, p100, p110, p111, color);
  addQuad(mesh, p010, p011, p111, p110, color);
  addQuad(mesh, p000, p100, p101, p001, color);
}

std::shared_ptr<EngineCore::Model> buildCornellBoxModel() {
  auto model = std::make_shared<EngineCore::Model>();
  EngineCore::Mesh mesh{};

  const glm::vec3 red(0.75f, 0.15f, 0.15f);
  const glm::vec3 green(0.15f, 0.75f, 0.15f);
  const glm::vec3 white(0.73f, 0.73f, 0.73f);

  const float xMin = -1.0f;
  const float xMax = 1.0f;
  const float yMin = 0.0f;
  const float yMax = 2.0f;
  const float zMin = -1.0f;
  const float zMax = 1.0f;

  addQuad(mesh, glm::vec3(xMin, yMin, zMin), glm::vec3(xMin, yMin, zMax), glm::vec3(xMax, yMin, zMax), glm::vec3(xMax, yMin, zMin), white);
  addQuad(mesh, glm::vec3(xMin, yMax, zMin), glm::vec3(xMax, yMax, zMin), glm::vec3(xMax, yMax, zMax), glm::vec3(xMin, yMax, zMax), white);
  addQuad(mesh, glm::vec3(xMin, yMin, zMin), glm::vec3(xMax, yMin, zMin), glm::vec3(xMax, yMax, zMin), glm::vec3(xMin, yMax, zMin), white);

  addQuad(mesh, glm::vec3(xMin, yMin, zMax), glm::vec3(xMin, yMin, zMin), glm::vec3(xMin, yMax, zMin), glm::vec3(xMin, yMax, zMax), red);
  addQuad(mesh, glm::vec3(xMax, yMin, zMin), glm::vec3(xMax, yMin, zMax), glm::vec3(xMax, yMax, zMax), glm::vec3(xMax, yMax, zMin), green);

  addBox(mesh, glm::vec3(-0.75f, 0.0f, -0.75f), glm::vec3(-0.2f, 0.9f, -0.2f), white);
  addBox(mesh, glm::vec3(0.15f, 0.0f, 0.15f), glm::vec3(0.75f, 1.5f, 0.75f), white);

  model->indexCount = static_cast<uint32_t>(mesh.indices.size());
  model->meshes.push_back(std::move(mesh));
  return model;
}

}  // namespace

int main() {
  GLFWwindow* window = nullptr;
  if (!initWindow(&window)) return 1;

  std::vector<std::string> validationLayers;
#ifdef _DEBUG
  validationLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif

  uint32_t glfwExtCount = 0;
  const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwExtCount);
  if (!glfwExt || glfwExtCount == 0) throw std::runtime_error("GLFW Vulkan extensions unavailable");

  std::vector<std::string> instanceExtensions;
  instanceExtensions.reserve(glfwExtCount + 2);
  for (uint32_t i = 0; i < glfwExtCount; ++i) instanceExtensions.emplace_back(glfwExt[i]);
  instanceExtensions.emplace_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_EXT_DEBUG_UTILS_EXTENSION_NAME
  instanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

  VulkanCore::Context::enableDefaultFeatures();
  VulkanCore::Context context(
      static_cast<void*>(window),
      validationLayers,
      instanceExtensions,
      {VK_KHR_SWAPCHAIN_EXTENSION_NAME},
      VK_QUEUE_GRAPHICS_BIT,
      true,
      false,
      "vkcornell");

  const VkExtent2D extents = context.physicalDevice().surfaceCapabilities().minImageExtent;
  const VkFormat swapChainFormat = VK_FORMAT_B8G8R8A8_UNORM;
  context.createSwapchain(swapChainFormat, VK_COLORSPACE_SRGB_NONLINEAR_KHR, VK_PRESENT_MODE_FIFO_KHR, extents);

  auto depthTexture = context.createTexture(
      VK_IMAGE_TYPE_2D,
      VK_FORMAT_D24_UNORM_S8_UINT,
      0,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VkExtent3D{extents.width, extents.height, 1},
      1,
      1,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      false,
      VK_SAMPLE_COUNT_1_BIT,
      "vkcornell depth");

  auto renderPass = context.createRenderPass(
      {context.swapchain()->texture(0), depthTexture},
      {VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_CLEAR},
      {VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_STORE_OP_DONT_CARE},
      {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
      VK_PIPELINE_BIND_POINT_GRAPHICS);

  std::vector<std::shared_ptr<VulkanCore::Framebuffer>> swapchainFramebuffers(context.swapchain()->numberImages());

#ifndef VKCORNELL_SHADER_DIR
#define VKCORNELL_SHADER_DIR ""
#endif
  const std::filesystem::path shaderDir = std::filesystem::path(VKCORNELL_SHADER_DIR);
  auto vertexShader = context.createShaderModule((shaderDir / "cornell.vert.spv").string(), VK_SHADER_STAGE_VERTEX_BIT, "cornell_vs");
  auto fragmentShader = context.createShaderModule((shaderDir / "cornell.frag.spv").string(), VK_SHADER_STAGE_FRAGMENT_BIT, "cornell_fs");

  const std::vector<VulkanCore::Pipeline::SetDescriptor> setLayout = {
      {
          .set_ = 0,
          .bindings_ = {
              VkDescriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT),
          },
      },
  };

  VkVertexInputBindingDescription bindingDesc{};
  bindingDesc.binding = 0;
  bindingDesc.stride = sizeof(EngineCore::Vertex);
  bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 2> attrs{};
  attrs[0].location = 0;
  attrs[0].binding = 0;
  attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[0].offset = offsetof(EngineCore::Vertex, pos);

  attrs[1].location = 1;
  attrs[1].binding = 0;
  attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[1].offset = offsetof(EngineCore::Vertex, normal);

  const VulkanCore::Pipeline::GraphicsPipelineDescriptor gpDesc = {
      .sets_ = setLayout,
      .vertexShader_ = vertexShader,
      .fragmentShader_ = fragmentShader,
      .colorTextureFormats = {swapChainFormat},
      .depthTextureFormat = VK_FORMAT_D24_UNORM_S8_UINT,
      .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_CLOCKWISE,
      .viewport = context.swapchain()->extent(),
      .depthTestEnable = true,
      .depthWriteEnable = true,
      .vertexInputCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
          .vertexBindingDescriptionCount = 1,
          .pVertexBindingDescriptions = &bindingDesc,
          .vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size()),
          .pVertexAttributeDescriptions = attrs.data(),
      },
  };

  auto pipeline = context.createGraphicsPipeline(gpDesc, renderPass->vkRenderPass(), "vkcornell pipeline");
  pipeline->allocateDescriptors({{0, 1, "camera_ubo"}});

  auto model = buildCornellBoxModel();
  const auto& mesh = model->meshes[0];

  auto vertexBuffer = context.createBuffer(
      mesh.vertices.size() * sizeof(EngineCore::Vertex),
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY,
      "cornell vertex");

  auto indexBuffer = context.createBuffer(
      mesh.indices.size() * sizeof(uint32_t),
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY,
      "cornell index");

  auto uniformBuffer = context.createPersistentBuffer(
      sizeof(CameraUBO),
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      "cornell camera ubo");

  auto uploadQueue = context.createGraphicsCommandQueue(2, 2, "cornell upload queue");
  {
    auto uploadCmd = uploadQueue.getCmdBufferToBegin();
    context.uploadToGPUBuffer(uploadQueue, uploadCmd, vertexBuffer.get(), mesh.vertices.data(), static_cast<long>(mesh.vertices.size() * sizeof(EngineCore::Vertex)));
    context.uploadToGPUBuffer(uploadQueue, uploadCmd, indexBuffer.get(), mesh.indices.data(), static_cast<long>(mesh.indices.size() * sizeof(uint32_t)));
    uploadQueue.endCmdBuffer(uploadCmd);

    constexpr VkPipelineStageFlags flags = VK_PIPELINE_STAGE_TRANSFER_BIT;
    auto submitInfo = context.swapchain()->createSubmitInfo(&uploadCmd, &flags, false, false);
    uploadQueue.submit(&submitInfo);
    uploadQueue.waitUntilAllSubmitsAreComplete();
  }

  pipeline->bindResource(0, 0, 0, uniformBuffer, 0, sizeof(CameraUBO), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

  auto commandMgr = context.createGraphicsCommandQueue(context.swapchain()->numberImages(), context.swapchain()->numberImages(), "vkcornell frame queue");

  EngineCore::Camera camera(glm::vec3(0.0f, 1.0f, 3.4f), glm::vec3(0.0f, 0.9f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0.1f, 20.0f,
                            static_cast<float>(extents.width) / static_cast<float>(extents.height));

  while (!glfwWindowShouldClose(window)) {
    auto texture = context.swapchain()->acquireImage();
    const auto imageIndex = context.swapchain()->currentImageIndex();

    if (!swapchainFramebuffers[imageIndex]) {
      swapchainFramebuffers[imageIndex] = context.createFramebuffer(renderPass->vkRenderPass(), {texture}, depthTexture, nullptr);
    }

    CameraUBO ubo{};
    const glm::mat4 modelM(1.0f);
    ubo.mvp = camera.getProjectMatrix() * camera.viewMatrix() * modelM;
    uniformBuffer->copyDataToBuffer(&ubo, sizeof(ubo));
    uniformBuffer->upload();

    auto cmd = commandMgr.getCmdBufferToBegin();

    const std::array<VkClearValue, 2> clearValues = {
        VkClearValue{.color = {{0.01f, 0.01f, 0.01f, 1.0f}}},
        VkClearValue{.depthStencil = {1.0f, 0}},
    };

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = renderPass->vkRenderPass();
    rpBegin.framebuffer = swapchainFramebuffers[imageIndex]->vkFramebuffer();
    rpBegin.renderArea = VkRect2D{VkOffset2D{0, 0}, extents};
    rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
    rpBegin.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    pipeline->bind(cmd);
    pipeline->bindDescriptorSets(cmd, {{0, 0}});
    pipeline->bindVertexBuffer(cmd, vertexBuffer->vkBuffer());
    pipeline->bindIndexBuffer(cmd, indexBuffer->vkBuffer());
    vkCmdDrawIndexed(cmd, static_cast<uint32_t>(mesh.indices.size()), 1, 0, 0, 0);

    vkCmdEndRenderPass(cmd);
    commandMgr.endCmdBuffer(cmd);

    constexpr VkPipelineStageFlags submitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    auto submitInfo = context.swapchain()->createSubmitInfo(&cmd, &submitStage);
    commandMgr.submit(&submitInfo);
    commandMgr.goToNextCmdBuffer();

    context.swapchain()->present();
    glfwPollEvents();
  }

  commandMgr.waitUntilAllSubmitsAreComplete();
  vkDeviceWaitIdle(context.device());

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
