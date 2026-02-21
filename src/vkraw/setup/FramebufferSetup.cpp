#include "vkraw/setup/FramebufferSetup.h"

#include <array>
#include <stdexcept>

namespace vkraw::setup {

void createFramebuffers(VkContext& context)
{
    context.swapchainFramebuffers.resize(context.swapchainImageViews.size());

    for (size_t i = 0; i < context.swapchainImageViews.size(); ++i) {
        std::array<VkImageView, 2> attachments{context.swapchainImageViews[i], context.depthImageView};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = context.renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = context.swapchain.extent.width;
        framebufferInfo.height = context.swapchain.extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(context.device.device, &framebufferInfo, nullptr, &context.swapchainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer");
        }
    }
}

} // namespace vkraw::setup
