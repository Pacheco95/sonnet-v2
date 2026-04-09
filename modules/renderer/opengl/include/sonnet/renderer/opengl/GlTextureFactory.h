#pragma once

#include <sonnet/api/render/IRendererBackend.h>
#include <sonnet/core/Macros.h>
#include <sonnet/renderer/opengl/GlTexture2D.h>

#include <memory>

namespace sonnet::renderer::opengl {

class GlTextureFactory final : public api::render::ITextureFactory {
public:
    GlTextureFactory() = default;

    SN_NON_COPYABLE(GlTextureFactory);
    SN_NON_MOVABLE(GlTextureFactory);

    [[nodiscard]] std::unique_ptr<api::render::ITexture> create(
        const api::render::TextureDesc &desc,
        const api::render::SamplerDesc &sampler,
        const api::render::CPUTextureBuffer &data) const override {
        return std::make_unique<GlTexture2D>(desc, sampler, data);
    }

    [[nodiscard]] std::unique_ptr<api::render::ITexture> create(
        const api::render::TextureDesc &desc,
        const api::render::SamplerDesc &sampler,
        const api::render::CubeMapFaces & /*faces*/) const override {
        // CubeMap support will be added when needed.
        (void)desc; (void)sampler;
        throw std::runtime_error("GlTextureFactory: CubeMap not yet implemented");
    }

    [[nodiscard]] std::unique_ptr<api::render::ITexture> create(
        const api::render::TextureDesc &desc,
        const api::render::SamplerDesc &sampler) const override {
        return std::make_unique<GlTexture2D>(desc, sampler);
    }
};

} // namespace sonnet::renderer::opengl
