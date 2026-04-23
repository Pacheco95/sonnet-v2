#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>

namespace sonnet::renderer::vulkan {

class Instance;

// Owns the physical + logical device, the graphics/present queues, and the
// VMA allocator. A single queue family is required to support both graphics
// and present on the given surface.
class Device {
public:
    Device(Instance &instance, VkSurfaceKHR surface);
    ~Device();

    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;

    [[nodiscard]] VkPhysicalDevice physical()      const { return m_physical; }
    [[nodiscard]] VkDevice         logical()       const { return m_device; }
    [[nodiscard]] VkQueue          graphicsQueue() const { return m_graphicsQueue; }
    [[nodiscard]] VkQueue          presentQueue()  const { return m_presentQueue; }
    [[nodiscard]] std::uint32_t    graphicsFamily() const { return m_graphicsFamily; }
    [[nodiscard]] std::uint32_t    presentFamily()  const { return m_presentFamily; }
    [[nodiscard]] VmaAllocator     allocator()     const { return m_allocator; }

    // Convenience: wait until all queued work completes.
    void waitIdle() const;

    // Synchronous one-shot command recording + submit + wait. Allocates a
    // transient primary command buffer from an internal pool, invokes `recorder`
    // between vkBegin/EndCommandBuffer, submits to the graphics queue, and
    // blocks until completion. Used by resource constructors (buffer/texture
    // staging uploads) where latency doesn't matter.
    void runOneShot(const std::function<void(VkCommandBuffer)> &recorder);

private:
    void pickPhysicalDevice(Instance &instance, VkSurfaceKHR surface);
    void createLogicalDevice();
    void createAllocator(Instance &instance);

    VkPhysicalDevice m_physical       = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;
    VkQueue          m_presentQueue   = VK_NULL_HANDLE;
    std::uint32_t    m_graphicsFamily = 0;
    std::uint32_t    m_presentFamily  = 0;
    VmaAllocator     m_allocator      = VK_NULL_HANDLE;
    bool             m_portabilitySubsetEnabled = false;

    // Transient pool for one-shot uploads (one per logical device).
    VkCommandPool    m_oneShotPool    = VK_NULL_HANDLE;

    void createOneShotPool();
};

} // namespace sonnet::renderer::vulkan
