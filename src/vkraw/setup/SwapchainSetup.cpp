#include "vkraw/setup/SwapchainSetup.h"

#include <iostream>
#include <stdexcept>

namespace vkraw::setup {

void createSwapchain(VkContext& context, GLFWwindow* window)
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window, &width, &height);
    }

    auto buildSwapchainWithMode = [&](VkPresentModeKHR mode, bool useOldSwapchain) {
        vkb::SwapchainBuilder builder(context.device);
        builder.set_desired_extent(static_cast<uint32_t>(width), static_cast<uint32_t>(height)).set_desired_present_mode(mode);
        if (useOldSwapchain && context.swapchain.swapchain != VK_NULL_HANDLE) {
            builder.set_old_swapchain(context.swapchain);
        }
        return builder.build();
    };

    auto swapchainRet = buildSwapchainWithMode(VK_PRESENT_MODE_IMMEDIATE_KHR, true);
    if (!swapchainRet) {
        swapchainRet = buildSwapchainWithMode(VK_PRESENT_MODE_FIFO_KHR, true);
    }
    if (!swapchainRet) {
        swapchainRet = buildSwapchainWithMode(VK_PRESENT_MODE_IMMEDIATE_KHR, false);
    }
    if (!swapchainRet) {
        swapchainRet = buildSwapchainWithMode(VK_PRESENT_MODE_FIFO_KHR, false);
    }
    if (!swapchainRet) {
        throw std::runtime_error(std::string("failed to create swapchain: ") + swapchainRet.error().message());
    }

    if (context.swapchain.swapchain != VK_NULL_HANDLE) {
        vkb::destroy_swapchain(context.swapchain);
    }

    context.swapchain = swapchainRet.value();
    context.selectedPresentMode = context.swapchain.present_mode;

    auto imagesRet = context.swapchain.get_images();
    auto imageViewsRet = context.swapchain.get_image_views();
    if (!imagesRet || !imageViewsRet) {
        throw std::runtime_error("failed to fetch swapchain images or views");
    }
    context.swapchainImages = imagesRet.value();
    context.swapchainImageViews = imageViewsRet.value();
}

} // namespace vkraw::setup
