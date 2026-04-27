#pragma once

#include <sonnet/api/render/IRenderTarget.h>
#include <sonnet/api/render/IRendererBackend.h>
#include <sonnet/core/Macros.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace sonnet::renderer::opengl {

class GlRenderTarget final : public api::render::IRenderTarget {
public:
    GlRenderTarget(const api::render::RenderTargetDesc &desc,
                   api::render::ITextureFactory &textureFactory);
    ~GlRenderTarget() override;

    SN_NON_COPYABLE(GlRenderTarget);
    SN_NON_MOVABLE(GlRenderTarget);

    [[nodiscard]] std::uint32_t  width()  const override;
    [[nodiscard]] std::uint32_t  height() const override;
    void                         bind()   const override;

    [[nodiscard]] const api::render::ITexture *colorTexture(std::size_t index) const override;
    [[nodiscard]] const api::render::ITexture *depthTexture()                  const override;

    void selectCubemapFace(std::uint32_t face, std::uint32_t mipLevel = 0) override;

    [[nodiscard]] std::array<std::uint8_t, 4> readPixelRGBA8(
        std::uint32_t attachmentIndex,
        std::uint32_t x, std::uint32_t y) const override;

private:
    void attachColorTextures(const api::render::RenderTargetDesc &desc,
                             api::render::ITextureFactory &factory);
    void attachDepth(const api::render::DepthAttachmentDesc &desc,
                     api::render::ITextureFactory &factory);

    std::uint32_t m_width;
    std::uint32_t m_height;
    unsigned      m_fbo = 0;

    std::vector<std::unique_ptr<api::render::ITexture>> m_colorTextures;
    std::unique_ptr<api::render::ITexture>              m_depthTexture;
    unsigned                                             m_depthRbo = 0;

    // ── Cubemap path ───────────────────────────────────────────────────────
    bool          m_isCubemap = false;
    std::uint32_t m_mipLevels = 1;
    // Updated by selectCubemapFace; bind() then re-attaches face/mip via
    // glFramebufferTexture2D before the first draw.
    mutable std::uint32_t m_activeFace = 0;
    mutable std::uint32_t m_activeMip  = 0;
};

} // namespace sonnet::renderer::opengl
