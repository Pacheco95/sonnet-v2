#pragma once

#include <sonnet/api/render/ITexture.h>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace sonnet::renderer::vulkan {

class Device;
class SamplerCache;
struct BindState;

// VMA-backed 2D image with a paired image view and a cached sampler.
// Phase 2 handles the Texture2D variant; cubemap faces throw until a
// follow-up task adds six-face upload (needed by IBL in Phase 7).
class VkTexture2D final : public api::render::ITexture {
public:
    // Upload from CPU data. Performs a one-shot staging copy and (optional)
    // mipmap generation via vkCmdBlitImage.
    VkTexture2D(Device &device, SamplerCache &samplers, BindState &bindState,
                const api::render::TextureDesc &desc,
                const api::render::SamplerDesc &sampler,
                const api::render::CPUTextureBuffer &data);

    // Allocate only (for render-target attachments). No staging copy; initial
    // layout is SHADER_READ_ONLY_OPTIMAL for color+sampled images and
    // DEPTH_STENCIL_ATTACHMENT_OPTIMAL for depth images (transitioned once
    // at construction via a one-shot barrier).
    VkTexture2D(Device &device, SamplerCache &samplers, BindState &bindState,
                const api::render::TextureDesc &desc,
                const api::render::SamplerDesc &sampler);

    ~VkTexture2D() override;

    VkTexture2D(const VkTexture2D &)            = delete;
    VkTexture2D &operator=(const VkTexture2D &) = delete;

    // ITexture
    void bind(std::uint8_t slot)   const override;
    void unbind(std::uint8_t slot) const override;

    [[nodiscard]] const api::render::TextureDesc &textureDesc() const override { return m_desc; }
    [[nodiscard]] const api::render::SamplerDesc &samplerDesc() const override { return m_sampler; }
    [[nodiscard]] unsigned        getNativeHandle() const override {
        // Vulkan has no single "native handle"; callers that want one reach
        // through backend-specific accessors below.
        return 0u;
    }
    [[nodiscard]] std::uintptr_t  getImGuiTextureId() override;

    // Backend-internal accessors.
    [[nodiscard]] VkImage     image()     const { return m_image; }
    [[nodiscard]] VkImageView imageView() const { return m_view; }
    [[nodiscard]] VkSampler   sampler()   const { return m_vkSampler; }
    [[nodiscard]] VkFormat    format()    const { return m_format; }
    [[nodiscard]] std::uint32_t mipLevels() const { return m_mipLevels; }

private:
    void generateMipmaps(VkCommandBuffer cmd);

    Device                    &m_device;
    BindState                 &m_bindState;
    api::render::TextureDesc   m_desc;
    api::render::SamplerDesc   m_sampler;

    VkImage                    m_image      = VK_NULL_HANDLE;
    VmaAllocation              m_alloc      = VK_NULL_HANDLE;
    VkImageView                m_view       = VK_NULL_HANDLE;
    VkSampler                  m_vkSampler  = VK_NULL_HANDLE; // borrowed; cached by SamplerCache
    VkFormat                   m_format     = VK_FORMAT_UNDEFINED;
    std::uint32_t              m_mipLevels  = 1;
    bool                       m_isDepth    = false;

    // Lazily-allocated ImGui descriptor (one per (sampler, view) pair). Cached
    // for the lifetime of the texture; freed back to imgui_impl_vulkan's
    // internal pool in the destructor.
    VkDescriptorSet            m_imguiDescriptor = VK_NULL_HANDLE;
};

} // namespace sonnet::renderer::vulkan
