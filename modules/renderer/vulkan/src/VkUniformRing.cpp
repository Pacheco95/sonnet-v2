#include "VkUniformRing.h"

#include "VkDevice.h"
#include "VkUtils.h"

namespace sonnet::renderer::vulkan {

namespace {
VkDeviceSize alignUp(VkDeviceSize n, VkDeviceSize a) {
    return (n + a - 1) & ~(a - 1);
}
} // namespace

UniformRing::UniformRing(Device &device, VkDeviceSize bytesPerFrame)
    : m_device(device), m_bytesPerFrame(bytesPerFrame) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device.physical(), &props);
    m_alignment = std::max<VkDeviceSize>(
        props.limits.minUniformBufferOffsetAlignment, 4);

    m_bytesPerFrame = alignUp(m_bytesPerFrame, m_alignment);
    const VkDeviceSize total = m_bytesPerFrame * kFramesInFlight;

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size  = total;
    info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    VmaAllocationCreateInfo allocCreate{};
    allocCreate.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocCreate.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                      | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo{};
    VK_CHECK(vmaCreateBuffer(device.allocator(), &info, &allocCreate,
                             &m_buffer, &m_alloc, &allocInfo));
    m_mapped = static_cast<std::uint8_t *>(allocInfo.pMappedData);
}

UniformRing::~UniformRing() {
    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_device.allocator(), m_buffer, m_alloc);
    }
}

void UniformRing::beginFrame(std::uint32_t frameIx) {
    m_currentFrame = frameIx % kFramesInFlight;
    m_cursor       = 0;
}

UniformRing::Allocation UniformRing::allocate(VkDeviceSize size) {
    const VkDeviceSize aligned = alignUp(size, m_alignment);
    if (m_cursor + aligned > m_bytesPerFrame) {
        throw VulkanError("UniformRing out of space — increase bytesPerFrame.");
    }

    const VkDeviceSize frameBase = static_cast<VkDeviceSize>(m_currentFrame) * m_bytesPerFrame;
    Allocation out{};
    out.mapped = m_mapped + frameBase + m_cursor;
    out.offset = frameBase + m_cursor;
    m_cursor  += aligned;
    return out;
}

} // namespace sonnet::renderer::vulkan
