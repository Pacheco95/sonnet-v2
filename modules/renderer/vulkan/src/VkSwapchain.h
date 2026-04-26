#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace sonnet::renderer::vulkan {

class Device;

// Owns the VkSwapchainKHR, per-image views and framebuffers, and a shared
// depth attachment. Also owns the "default" render pass used for the
// swapchain (single color + depth). Supports recreate on resize.
class Swapchain {
public:
    Swapchain(Device &device, VkSurfaceKHR surface,
              std::uint32_t fbWidth, std::uint32_t fbHeight);
    ~Swapchain();

    Swapchain(const Swapchain &) = delete;
    Swapchain &operator=(const Swapchain &) = delete;

    // Acquire the next image. Returns the same VkResult as vkAcquireNextImageKHR
    // so the caller can react to OUT_OF_DATE/SUBOPTIMAL.
    VkResult acquireNextImage(VkSemaphore imageAvailable, std::uint32_t &outIndex);

    // Submit a present. Caller passes the index returned by acquireNextImage()
    // and the semaphore to wait on. Returns the VkResult from vkQueuePresentKHR.
    VkResult present(std::uint32_t imageIndex, VkSemaphore waitSemaphore);

    // Destroy current resources and rebuild them at a new framebuffer size.
    // Caller is responsible for ensuring no pending GPU work references the
    // old swapchain images (e.g. vkDeviceWaitIdle first).
    void recreate(std::uint32_t fbWidth, std::uint32_t fbHeight);

    [[nodiscard]] VkFormat     colorFormat()  const { return m_colorFormat; }
    [[nodiscard]] VkFormat     depthFormat()  const { return m_depthFormat; }
    [[nodiscard]] VkExtent2D   extent()       const { return m_extent; }
    [[nodiscard]] std::uint32_t imageCount()  const { return static_cast<std::uint32_t>(m_images.size()); }
    [[nodiscard]] VkRenderPass defaultRenderPass() const { return m_defaultRenderPass; }
    [[nodiscard]] VkFramebuffer framebuffer(std::uint32_t i) const { return m_framebuffers[i]; }
    // Per-image semaphore signaled by the queue submit and waited on by the
    // present. Per-image (not per-frame-in-flight) so that with N images and
    // M < N frame slots, a semaphore is never signaled while a previous
    // present still holds it.
    [[nodiscard]] VkSemaphore  renderFinished(std::uint32_t i) const { return m_renderFinished[i]; }

private:
    void create(std::uint32_t fbWidth, std::uint32_t fbHeight);
    void destroy();
    void createImageViews();
    void createDepthResources();
    void createDefaultRenderPass();
    void createFramebuffers();

    void chooseSurfaceFormat();
    void choosePresentMode();
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR &caps,
                            std::uint32_t fbWidth, std::uint32_t fbHeight) const;

    Device                       &m_device;
    VkSurfaceKHR                  m_surface;

    VkSwapchainKHR                m_swapchain  = VK_NULL_HANDLE;
    VkFormat                      m_colorFormat = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR               m_colorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR              m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D                    m_extent{0, 0};

    std::vector<VkImage>          m_images;
    std::vector<VkImageView>      m_imageViews;

    VkImage                       m_depthImage    = VK_NULL_HANDLE;
    VmaAllocation                 m_depthAlloc    = VK_NULL_HANDLE;
    VkImageView                   m_depthView     = VK_NULL_HANDLE;
    VkFormat                      m_depthFormat   = VK_FORMAT_UNDEFINED;

    VkRenderPass                  m_defaultRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>    m_framebuffers;

    std::vector<VkSemaphore>      m_renderFinished;
};

} // namespace sonnet::renderer::vulkan
