#include <sonnet/renderer/vulkan/VkGpuBuffer.h>

#include "VkBindState.h"
#include "VkDevice.h"
#include "VkUtils.h"

#include <cstring>

namespace sonnet::renderer::vulkan {

namespace {

VkBufferUsageFlags usageFor(api::render::BufferType type) {
    switch (type) {
        case api::render::BufferType::Vertex:
            return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        case api::render::BufferType::Index:
            return VK_BUFFER_USAGE_INDEX_BUFFER_BIT  | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        case api::render::BufferType::Uniform:
            return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    return 0;
}

bool isDynamic(api::render::BufferType type) {
    return type == api::render::BufferType::Uniform;
}

} // namespace

VkGpuBuffer::VkGpuBuffer(Device &device, BindState &bindState,
                         api::render::BufferType type,
                         const void *data, std::size_t size)
    : m_device(device), m_bindState(bindState), m_type(type), m_size(size) {
    if (size == 0) {
        throw VulkanError("VkGpuBuffer: zero-size buffer");
    }

    VkBufferCreateInfo info{};
    info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size        = size;
    info.usage       = usageFor(type);
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCreate{};
    VmaAllocationInfo       allocInfo{};
    if (isDynamic(type)) {
        // Persistently-mapped, host-coherent. Renderer.cpp updates UBOs every
        // frame via IGpuBuffer::update(); no staging needed.
        allocCreate.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocCreate.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                          | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VK_CHECK(vmaCreateBuffer(device.allocator(), &info, &allocCreate,
                                 &m_buffer, &m_alloc, &allocInfo));
        m_mapped = allocInfo.pMappedData;
        if (data) {
            std::memcpy(m_mapped, data, size);
        }
    } else {
        // Static vertex/index: DEVICE_LOCAL dest + staging upload.
        allocCreate.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateBuffer(device.allocator(), &info, &allocCreate,
                                 &m_buffer, &m_alloc, nullptr));

        if (data) {
            // Staging buffer (host-visible, TRANSFER_SRC).
            VkBufferCreateInfo stagingInfo{};
            stagingInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            stagingInfo.size        = size;
            stagingInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo stagingAllocCreate{};
            stagingAllocCreate.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            stagingAllocCreate.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                                     | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VkBuffer          stagingBuf   = VK_NULL_HANDLE;
            VmaAllocation     stagingAlloc = VK_NULL_HANDLE;
            VmaAllocationInfo stagingInfoOut{};
            VK_CHECK(vmaCreateBuffer(device.allocator(), &stagingInfo, &stagingAllocCreate,
                                     &stagingBuf, &stagingAlloc, &stagingInfoOut));
            std::memcpy(stagingInfoOut.pMappedData, data, size);

            device.runOneShot([&](VkCommandBuffer cmd) {
                VkBufferCopy region{};
                region.size = size;
                vkCmdCopyBuffer(cmd, stagingBuf, m_buffer, 1, &region);
            });

            vmaDestroyBuffer(device.allocator(), stagingBuf, stagingAlloc);
        }
    }
}

VkGpuBuffer::~VkGpuBuffer() {
    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_device.allocator(), m_buffer, m_alloc);
    }
}

void VkGpuBuffer::bind() const {
    switch (m_type) {
        case api::render::BufferType::Vertex:  m_bindState.currentVertex = m_buffer; break;
        case api::render::BufferType::Index:
            m_bindState.currentIndex = m_buffer;
            m_bindState.indexType    = VK_INDEX_TYPE_UINT32; // Engine invariant.
            break;
        case api::render::BufferType::Uniform:
            // No-op: UBO binding is explicit via bindBase.
            break;
    }
}

void VkGpuBuffer::update(const void *data, std::size_t size) {
    if (!isDynamic(m_type)) {
        throw VulkanError("VkGpuBuffer::update on a static (Vertex/Index) buffer");
    }
    if (size > m_size) {
        throw VulkanError("VkGpuBuffer::update size exceeds buffer capacity");
    }
    std::memcpy(m_mapped, data, size);
}

void VkGpuBuffer::bindBase(std::uint32_t bindingPoint) const {
    if (bindingPoint >= BindState::kMaxUboBindings) {
        throw VulkanError("VkGpuBuffer::bindBase: binding point exceeds kMaxUboBindings");
    }
    m_bindState.ubos[bindingPoint]     = m_buffer;
    m_bindState.uboSizes[bindingPoint] = static_cast<VkDeviceSize>(m_size);
}

} // namespace sonnet::renderer::vulkan
