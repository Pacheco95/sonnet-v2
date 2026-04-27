#pragma once

#include <sonnet/api/render/IRenderTarget.h>
#include <sonnet/renderer/vulkan/VkTexture2D.h>

#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

namespace sonnet::renderer::vulkan {

class Device;
class SamplerCache;
struct BindState;

// Offscreen render target: owns its color and optional depth VkTexture2D
// attachments, plus a VkRenderPass and VkFramebuffer built at construction
// from the attachment formats. Clear semantics default to LOAD_OP_CLEAR;
// final layouts are SHADER_READ_ONLY for colors (expected to be sampled
// downstream) and DEPTH_STENCIL_ATTACHMENT for depth (also sampled, but
// transitioned via a barrier when sampled — Phase 3 wires that up).
//
// Cubemap path (desc.isCubemap == true): the color texture (and the depth
// texture if it's a TextureAttachmentDesc) are allocated as cubemaps with
// `desc.mipLevels` mip levels. The RT pre-builds one VkFramebuffer per
// (face, mip) combination, each over a single-face single-mip VkImageView.
// `selectCubemapFace(face, mip)` chooses which one `framebuffer()` returns
// next; the backend's bindRenderTarget reads the active framebuffer/extent.
class VkRenderTarget final : public api::render::IRenderTarget {
public:
    VkRenderTarget(Device &device, SamplerCache &samplers, BindState &bindState,
                   const api::render::RenderTargetDesc &desc);
    ~VkRenderTarget() override;

    VkRenderTarget(const VkRenderTarget &)            = delete;
    VkRenderTarget &operator=(const VkRenderTarget &) = delete;

    // IRenderTarget
    [[nodiscard]] std::uint32_t width()  const override;
    [[nodiscard]] std::uint32_t height() const override;
    void                         bind()   const override; // no-op; backend-driven.

    [[nodiscard]] const api::render::ITexture *colorTexture(std::size_t index) const override {
        return index < m_colors.size() ? m_colors[index].get() : nullptr;
    }
    [[nodiscard]] const api::render::ITexture *depthTexture() const override {
        return m_depth.get();
    }

    void selectCubemapFace(std::uint32_t face, std::uint32_t mipLevel = 0) override;

    [[nodiscard]] std::array<std::uint8_t, 4> readPixelRGBA8(
        std::uint32_t attachmentIndex,
        std::uint32_t x, std::uint32_t y) const override;

    // Backend-internal accessors.
    [[nodiscard]] VkRenderPass  renderPass()  const { return m_renderPass; }
    [[nodiscard]] VkFramebuffer framebuffer() const;
    [[nodiscard]] std::uint32_t colorCount()  const { return static_cast<std::uint32_t>(m_colors.size()); }
    [[nodiscard]] bool          hasDepth()    const { return m_depth != nullptr; }

private:
    void buildRenderPass();
    void buildFramebuffer();
    void buildCubemapFramebuffers(); // when m_isCubemap

    Device                                    &m_device;
    std::uint32_t                              m_width;
    std::uint32_t                              m_height;
    std::vector<std::unique_ptr<VkTexture2D>>  m_colors;
    std::unique_ptr<VkTexture2D>               m_depth;
    VkRenderPass                               m_renderPass  = VK_NULL_HANDLE;
    VkFramebuffer                              m_framebuffer = VK_NULL_HANDLE;

    // ── Cubemap path ───────────────────────────────────────────────────────
    bool                          m_isCubemap = false;
    std::uint32_t                 m_mipLevels = 1;
    // Indexed as [mip * 6 + face]. Each VkFramebuffer points at one face/mip
    // via its single-layer single-mip VkImageView. Size = mipLevels × 6 when
    // cubemap, empty otherwise.
    std::vector<VkFramebuffer>    m_faceFramebuffers;
    std::vector<VkImageView>      m_faceColorViews;
    std::vector<VkImageView>      m_faceDepthViews; // only when m_depth is a cubemap

    mutable std::uint32_t         m_activeFace = 0;
    mutable std::uint32_t         m_activeMip  = 0;
};

} // namespace sonnet::renderer::vulkan
