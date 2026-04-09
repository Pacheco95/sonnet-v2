#pragma once

#include <sonnet/api/render/IRendererBackend.h>
#include <sonnet/core/Macros.h>
#include <sonnet/renderer/opengl/GlRenderTarget.h>

#include <memory>

namespace sonnet::renderer::opengl {

class GlRenderTargetFactory final : public api::render::IRenderTargetFactory {
public:
    explicit GlRenderTargetFactory(api::render::ITextureFactory &textureFactory)
        : m_textureFactory(textureFactory) {}

    SN_NON_COPYABLE(GlRenderTargetFactory);
    SN_NON_MOVABLE(GlRenderTargetFactory);

    [[nodiscard]] std::unique_ptr<api::render::IRenderTarget> create(
        const api::render::RenderTargetDesc &desc) const override {
        return std::make_unique<GlRenderTarget>(desc, m_textureFactory);
    }

private:
    api::render::ITextureFactory &m_textureFactory;
};

} // namespace sonnet::renderer::opengl
