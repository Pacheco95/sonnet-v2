#include "VkSwapchain.h"

#include "VkDevice.h"
#include "VkUtils.h"

#include <algorithm>
#include <array>

namespace sonnet::renderer::vulkan {

namespace {

VkFormat pickDepthFormat(VkPhysicalDevice dev) {
    // Preferred → fallback. All supported by essentially every desktop GPU.
    constexpr std::array candidates{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    for (VkFormat f : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(dev, f, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return f;
        }
    }
    throw VulkanError("no supported depth format");
}

} // namespace

Swapchain::Swapchain(Device &device, VkSurfaceKHR surface,
                     std::uint32_t fbWidth, std::uint32_t fbHeight)
    : m_device(device), m_surface(surface) {
    m_depthFormat = pickDepthFormat(device.physical());
    create(fbWidth, fbHeight);
}

Swapchain::~Swapchain() { destroy(); }

void Swapchain::recreate(std::uint32_t fbWidth, std::uint32_t fbHeight) {
    destroy();
    create(fbWidth, fbHeight);
}

VkResult Swapchain::acquireNextImage(VkSemaphore imageAvailable,
                                     std::uint32_t &outIndex) {
    return vkAcquireNextImageKHR(m_device.logical(), m_swapchain, UINT64_MAX,
                                 imageAvailable, VK_NULL_HANDLE, &outIndex);
}

VkResult Swapchain::present(std::uint32_t imageIndex, VkSemaphore waitSemaphore) {
    VkPresentInfoKHR info{};
    info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores    = &waitSemaphore;
    info.swapchainCount     = 1;
    info.pSwapchains        = &m_swapchain;
    info.pImageIndices      = &imageIndex;
    return vkQueuePresentKHR(m_device.presentQueue(), &info);
}

void Swapchain::create(std::uint32_t fbWidth, std::uint32_t fbHeight) {
    chooseSurfaceFormat();
    choosePresentMode();

    VkSurfaceCapabilitiesKHR caps{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        m_device.physical(), m_surface, &caps));
    m_extent = chooseExtent(caps, fbWidth, fbHeight);

    std::uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR info{};
    info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface          = m_surface;
    info.minImageCount    = imageCount;
    info.imageFormat      = m_colorFormat;
    info.imageColorSpace  = m_colorSpace;
    info.imageExtent      = m_extent;
    info.imageArrayLayers = 1;
    info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const std::uint32_t families[2] = {m_device.graphicsFamily(), m_device.presentFamily()};
    if (m_device.graphicsFamily() != m_device.presentFamily()) {
        info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices   = families;
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    info.preTransform   = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode    = m_presentMode;
    info.clipped        = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(m_device.logical(), &info, nullptr, &m_swapchain));

    std::uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(m_device.logical(), m_swapchain, &actualCount, nullptr);
    m_images.resize(actualCount);
    vkGetSwapchainImagesKHR(m_device.logical(), m_swapchain, &actualCount, m_images.data());

    spdlog::info("[vulkan] swapchain created ({}x{}, format={}, imageCount={}, presentMode={})",
                 m_extent.width, m_extent.height,
                 static_cast<int>(m_colorFormat), actualCount,
                 static_cast<int>(m_presentMode));

    createImageViews();
    createDepthResources();
    createDefaultRenderPass();
    createFramebuffers();

    m_renderFinished.resize(m_images.size());
    for (auto &sem : m_renderFinished) {
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(m_device.logical(), &semInfo, nullptr, &sem));
    }
}

void Swapchain::destroy() {
    VkDevice d = m_device.logical();

    for (auto sem : m_renderFinished) {
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(d, sem, nullptr);
    }
    m_renderFinished.clear();

    for (auto fb : m_framebuffers) {
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(d, fb, nullptr);
    }
    m_framebuffers.clear();

    if (m_defaultRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(d, m_defaultRenderPass, nullptr);
        m_defaultRenderPass = VK_NULL_HANDLE;
    }

    if (m_depthView != VK_NULL_HANDLE) {
        vkDestroyImageView(d, m_depthView, nullptr);
        m_depthView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_device.allocator(), m_depthImage, m_depthAlloc);
        m_depthImage = VK_NULL_HANDLE;
        m_depthAlloc = VK_NULL_HANDLE;
    }

    for (auto v : m_imageViews) {
        if (v != VK_NULL_HANDLE) vkDestroyImageView(d, v, nullptr);
    }
    m_imageViews.clear();
    m_images.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(d, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void Swapchain::createImageViews() {
    m_imageViews.resize(m_images.size());
    for (std::size_t i = 0; i < m_images.size(); ++i) {
        VkImageViewCreateInfo info{};
        info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image    = m_images[i];
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format   = m_colorFormat;
        info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel   = 0;
        info.subresourceRange.levelCount     = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount     = 1;
        VK_CHECK(vkCreateImageView(m_device.logical(), &info, nullptr, &m_imageViews[i]));
    }
}

void Swapchain::createDepthResources() {
    VkImageCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType     = VK_IMAGE_TYPE_2D;
    info.format        = m_depthFormat;
    info.extent        = {m_extent.width, m_extent.height, 1};
    info.mipLevels     = 1;
    info.arrayLayers   = 1;
    info.samples       = VK_SAMPLE_COUNT_1_BIT;
    info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    info.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    VK_CHECK(vmaCreateImage(m_device.allocator(), &info, &allocInfo,
                            &m_depthImage, &m_depthAlloc, nullptr));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = m_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = m_depthFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;
    VK_CHECK(vkCreateImageView(m_device.logical(), &viewInfo, nullptr, &m_depthView));
}

void Swapchain::createDefaultRenderPass() {
    VkAttachmentDescription color{};
    color.format         = m_colorFormat;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format         = m_depthFormat;
    depth.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                      | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                      | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    const std::array<VkAttachmentDescription, 2> attachments{color, depth};

    VkRenderPassCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    info.pAttachments    = attachments.data();
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = 1;
    info.pDependencies   = &dep;
    VK_CHECK(vkCreateRenderPass(m_device.logical(), &info, nullptr, &m_defaultRenderPass));
}

void Swapchain::createFramebuffers() {
    m_framebuffers.resize(m_imageViews.size());
    for (std::size_t i = 0; i < m_imageViews.size(); ++i) {
        const std::array<VkImageView, 2> attachments{m_imageViews[i], m_depthView};
        VkFramebufferCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass      = m_defaultRenderPass;
        info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        info.pAttachments    = attachments.data();
        info.width           = m_extent.width;
        info.height          = m_extent.height;
        info.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(m_device.logical(), &info, nullptr, &m_framebuffers[i]));
    }
}

void Swapchain::chooseSurfaceFormat() {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_device.physical(), m_surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_device.physical(), m_surface, &count, formats.data());

    for (const auto &f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
             f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            m_colorFormat = f.format;
            m_colorSpace  = f.colorSpace;
            return;
        }
    }
    // Fall back to whatever the surface reports first.
    if (!formats.empty()) {
        m_colorFormat = formats[0].format;
        m_colorSpace  = formats[0].colorSpace;
    } else {
        throw VulkanError("surface reports no formats");
    }
}

void Swapchain::choosePresentMode() {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_device.physical(), m_surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_device.physical(), m_surface, &count, modes.data());

    // FIFO is guaranteed on every Vulkan implementation; just use it.
    m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    (void)modes;
}

VkExtent2D Swapchain::chooseExtent(const VkSurfaceCapabilitiesKHR &caps,
                                   std::uint32_t fbWidth, std::uint32_t fbHeight) const {
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }
    VkExtent2D ext{fbWidth, fbHeight};
    ext.width  = std::clamp(ext.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    ext.height = std::clamp(ext.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return ext;
}

} // namespace sonnet::renderer::vulkan
