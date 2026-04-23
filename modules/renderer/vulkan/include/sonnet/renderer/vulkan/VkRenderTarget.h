#pragma once

#include <sonnet/api/render/IRenderTarget.h>
#include <sonnet/renderer/vulkan/VkTexture2D.h>

#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

namespace sonnet::renderer::vulkan {

class Device;
class SamplerCache;

// Offscreen render target: owns its color and optional depth VkTexture2D
// attachments, plus a VkRenderPass and VkFramebuffer built at construction
// from the attachment formats. Clear semantics default to LOAD_OP_CLEAR;
// final layouts are SHADER_READ_ONLY for colors (expected to be sampled
// downstream) and DEPTH_STENCIL_ATTACHMENT for depth (also sampled, but
// transitioned via a barrier when sampled — Phase 3 wires that up).
class VkRenderTarget final : public api::render::IRenderTarget {
public:
    VkRenderTarget(Device &device, SamplerCache &samplers,
                   const api::render::RenderTargetDesc &desc);
    ~VkRenderTarget() override;

    VkRenderTarget(const VkRenderTarget &)            = delete;
    VkRenderTarget &operator=(const VkRenderTarget &) = delete;

    // IRenderTarget
    [[nodiscard]] std::uint32_t width()  const override { return m_width; }
    [[nodiscard]] std::uint32_t height() const override { return m_height; }
    void                         bind()   const override; // no-op; backend-driven.

    [[nodiscard]] const api::render::ITexture *colorTexture(std::size_t index) const override {
        return index < m_colors.size() ? m_colors[index].get() : nullptr;
    }
    [[nodiscard]] const api::render::ITexture *depthTexture() const override {
        return m_depth.get();
    }

    // Backend-internal accessors.
    [[nodiscard]] VkRenderPass  renderPass()  const { return m_renderPass; }
    [[nodiscard]] VkFramebuffer framebuffer() const { return m_framebuffer; }
    [[nodiscard]] std::uint32_t colorCount()  const { return static_cast<std::uint32_t>(m_colors.size()); }
    [[nodiscard]] bool          hasDepth()    const { return m_depth != nullptr; }

private:
    void buildRenderPass();
    void buildFramebuffer();

    Device                                    &m_device;
    std::uint32_t                              m_width;
    std::uint32_t                              m_height;
    std::vector<std::unique_ptr<VkTexture2D>>  m_colors;
    std::unique_ptr<VkTexture2D>               m_depth;
    VkRenderPass                               m_renderPass  = VK_NULL_HANDLE;
    VkFramebuffer                              m_framebuffer = VK_NULL_HANDLE;
};

} // namespace sonnet::renderer::vulkan
