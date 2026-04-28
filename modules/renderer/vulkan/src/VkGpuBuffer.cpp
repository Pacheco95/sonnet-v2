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
            // TRANSFER_DST so update() can route through vkCmdUpdateBuffer
            // (serialized with the active command buffer) — required because
            // the engine binds a single CameraUBO/LightsUBO across many
            // passes per frame and a host memcpy would let later passes'
            // matrices overwrite earlier ones at GPU execution time.
            return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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

    // Inside a frame, route the write through vkCmdUpdateBuffer so it
    // serializes with the surrounding draws in command-buffer order. With a
    // single shared UBO across many passes per frame, a host memcpy would
    // otherwise let later passes' writes overwrite the data earlier passes'
    // recorded draws would read at GPU execution time.
    //
    // vkCmdUpdateBuffer is illegal inside an active render pass (Vulkan spec
    // VUID-vkCmdUpdateBuffer-renderpass). The demo's deferred-pass model
    // does have one in-pass multi-render pattern: PostProcess.cpp's hdrRT
    // pass renders the deferred quad, then sky, then the outline composite
    // back-to-back without re-binding. Each of those uses the same scene
    // FrameContext, so the UBO write would be a duplicate; dropping it is
    // functionally equivalent. A future Phase-8 ring buffer (ringed UBO
    // slots per pass) would let in-pass updates land on a fresh slot
    // instead of being skipped. Until then, this is the cheapest correct
    // behaviour.
    if (m_bindState.currentCmd != VK_NULL_HANDLE && !m_bindState.passActive) {
        // Spec requires size to be a multiple of 4 and <= 65536. Sonnet's
        // UBOs are naturally 16-byte aligned (std140) and well under 64 KB,
        // so the guard is a sanity check.
        if ((size % 4) != 0 || size > 65536) {
            throw VulkanError("VkGpuBuffer::update size unsupported by vkCmdUpdateBuffer "
                              "(must be multiple of 4 and <= 65536)");
        }
        vkCmdUpdateBuffer(m_bindState.currentCmd, m_buffer, 0,
                          static_cast<VkDeviceSize>(size), data);
        return;
    }
    if (m_bindState.currentCmd != VK_NULL_HANDLE && m_bindState.passActive) {
        // In-pass write: drop and rely on the previous out-of-pass update.
        // (See comment above — only safe because the demo's same-RT renders
        // share the FrameContext across calls.)
        return;
    }

    // Out of frame (init-time fill, e.g. main_vk.cpp's static UBO): host
    // memcpy into the persistently-mapped allocation. This path is also
    // taken if a caller bypasses the engine's frame lifecycle.
    if (m_mapped == nullptr) {
        throw VulkanError("VkGpuBuffer::update outside a frame requires a host-mapped "
                          "buffer (allocate-only ctor with HOST access)");
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
