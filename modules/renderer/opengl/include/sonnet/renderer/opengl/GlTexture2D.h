#pragma once

#include <sonnet/api/render/ITexture.h>
#include <sonnet/core/Macros.h>

namespace sonnet::renderer::opengl {

class GlTexture2D final : public api::render::ITexture {
public:
    // From CPU data.
    GlTexture2D(const api::render::TextureDesc &desc,
                const api::render::SamplerDesc &sampler,
                const api::render::CPUTextureBuffer &data);

    // Allocate-only (for render targets).
    GlTexture2D(const api::render::TextureDesc &desc,
                const api::render::SamplerDesc &sampler);

    ~GlTexture2D() override;

    SN_NON_COPYABLE(GlTexture2D);
    SN_NON_MOVABLE(GlTexture2D);

    void bind(std::uint8_t slot)   const override;
    void unbind(std::uint8_t slot) const override;

    [[nodiscard]] const api::render::TextureDesc &textureDesc() const override { return m_textureDesc; }
    [[nodiscard]] const api::render::SamplerDesc &samplerDesc() const override { return m_samplerDesc; }
    [[nodiscard]] unsigned getNativeHandle()                    const override { return m_texture; }

private:
    unsigned                    m_texture    = 0;
    api::render::TextureDesc    m_textureDesc;
    api::render::SamplerDesc    m_samplerDesc;

    void applySamplerState() const;
};

} // namespace sonnet::renderer::opengl
