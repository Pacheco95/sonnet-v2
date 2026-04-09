#pragma once

#include <sonnet/api/render/IRenderTarget.h>
#include <sonnet/api/render/IRendererBackend.h>
#include <sonnet/core/Macros.h>

#include <glad/glad.h>

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

    [[nodiscard]] std::uint32_t  width()  const override { return m_width; }
    [[nodiscard]] std::uint32_t  height() const override { return m_height; }
    void                         bind()   const override;

    [[nodiscard]] const api::render::ITexture *colorTexture(std::size_t index) const override;
    [[nodiscard]] const api::render::ITexture *depthTexture()                  const override;

private:
    void attachColorTextures(const api::render::RenderTargetDesc &desc,
                             api::render::ITextureFactory &factory);
    void attachDepth(const api::render::DepthAttachmentDesc &desc,
                     api::render::ITextureFactory &factory);

    std::uint32_t m_width;
    std::uint32_t m_height;
    GLuint        m_fbo = 0;

    std::vector<std::unique_ptr<api::render::ITexture>> m_colorTextures;
    std::unique_ptr<api::render::ITexture>              m_depthTexture;
    GLuint                                               m_depthRbo = 0;
};

} // namespace sonnet::renderer::opengl
