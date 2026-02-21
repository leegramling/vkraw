#include "vkraw/VkVisualizerApp.h"

#include "vkraw/CubeRenderTypes.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <array>
#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>

namespace vkraw {

void VkVisualizerApp::processInput(float deltaSeconds) {
    cube_.processInput(context_.window, deltaSeconds);
}

void VkVisualizerApp::updateUniformBuffer() {
    UniformBufferObject ubo{};

    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 220.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection =
        glm::perspective(glm::radians(60.0f), context_.swapchain.extent.width / static_cast<float>(context_.swapchain.extent.height), 0.1f, 2000.0f);
    projection[1][1] *= -1.0f;

    ubo.viewProj = projection * view;
    uploadToMemory(context_.uniformBufferMemory, &ubo, sizeof(ubo));
}

glm::mat4 VkVisualizerApp::computeBaseRotation(float elapsedSeconds) const {
    return cube_.computeBaseRotation(elapsedSeconds);
}

void VkVisualizerApp::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, float elapsedSeconds, size_t frameIndex) {
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
    vkCmdBindIndexBuffer(commandBuffer, context_.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context_.pipelineLayout, 0, 1, &context_.descriptorSet, 0, nullptr);

    const glm::mat4 baseRotation = computeBaseRotation(elapsedSeconds);
    PushConstantData push{};
    push.model = baseRotation;
    vkCmdPushConstants(commandBuffer, context_.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantData), &push);
    vkCmdDrawIndexed(commandBuffer, sceneIndexCount_, 1, 0, 0, 0);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    if (context_.gpuTimestampQueryPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, context_.gpuTimestampQueryPool, queryStart + 1);
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer");
    }
}

void VkVisualizerApp::drawFrame(float deltaSeconds, float elapsedSeconds) {
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
        rebuildGpuMeshBuffers();
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

} // namespace vkraw
