#pragma once

#include <sonnet/api/render/IGpuBuffer.h>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace sonnet::renderer::vulkan {

class Device;
struct BindState;

// VMA-backed GPU buffer.
// - Vertex/Index: DEVICE_LOCAL, uploaded once via staging in the ctor.
//   update() throws: static buffers should not be rewritten post-construction.
// - Uniform:      HOST_VISIBLE + persistently mapped. update() memcpy's into
//   the mapped region. bindBase(n) registers this buffer at UBO slot n in the
//   shared BindState; drawIndexed consumes that state at descriptor-build time.
class VkGpuBuffer final : public api::render::IGpuBuffer {
public:
    VkGpuBuffer(Device &device, BindState &bindState,
                api::render::BufferType type,
                const void *data, std::size_t size);
    ~VkGpuBuffer() override;

    VkGpuBuffer(const VkGpuBuffer &)            = delete;
    VkGpuBuffer &operator=(const VkGpuBuffer &) = delete;

    // IGpuBuffer
    void bind()                                          const override;
    void update(const void *data, std::size_t size)            override;
    void bindBase(std::uint32_t bindingPoint)            const override;

    // Backend-internal accessors.
    [[nodiscard]] VkBuffer                 buffer()  const { return m_buffer; }
    [[nodiscard]] std::size_t              size()    const { return m_size; }
    [[nodiscard]] api::render::BufferType  type()    const { return m_type; }

private:
    Device                      &m_device;
    BindState                   &m_bindState;
    api::render::BufferType      m_type;
    VkBuffer                     m_buffer      = VK_NULL_HANDLE;
    VmaAllocation                m_alloc       = VK_NULL_HANDLE;
    void                        *m_mapped      = nullptr; // persistent map (uniform only)
    std::size_t                  m_size        = 0;
};

} // namespace sonnet::renderer::vulkan
