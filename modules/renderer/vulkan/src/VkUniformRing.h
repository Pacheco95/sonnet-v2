#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace sonnet::renderer::vulkan {

class Device;

// Per-frame uniform ring allocator. The ring is one persistently-mapped
// HOST_VISIBLE+HOST_COHERENT VkBuffer split into `kFramesInFlight` slices;
// beginFrame(f) resets slice f's cursor. allocate(size) bumps the cursor
// with minUniformBufferOffsetAlignment padding and returns a mapped pointer
// plus the byte offset within the whole buffer — both fed into a descriptor
// set (one per draw) when binding set=2 with dynamic offset.
class UniformRing {
public:
    static constexpr std::uint32_t kFramesInFlight = 2;

    UniformRing(Device &device, VkDeviceSize bytesPerFrame);
    ~UniformRing();

    UniformRing(const UniformRing &)            = delete;
    UniformRing &operator=(const UniformRing &) = delete;

    struct Allocation {
        std::uint8_t *mapped = nullptr;
        VkDeviceSize  offset = 0; // bytes from start of the whole buffer
    };

    void  beginFrame(std::uint32_t frameIx);
    Allocation allocate(VkDeviceSize size);

    [[nodiscard]] VkBuffer     buffer()        const { return m_buffer; }
    [[nodiscard]] VkDeviceSize bytesPerFrame() const { return m_bytesPerFrame; }

private:
    Device       &m_device;
    VkBuffer      m_buffer        = VK_NULL_HANDLE;
    VmaAllocation m_alloc         = VK_NULL_HANDLE;
    std::uint8_t *m_mapped        = nullptr;
    VkDeviceSize  m_bytesPerFrame = 0;
    VkDeviceSize  m_alignment     = 256; // queried from physical-device limits
    std::uint32_t m_currentFrame  = 0;
    VkDeviceSize  m_cursor        = 0; // offset within current frame's slice
};

} // namespace sonnet::renderer::vulkan
