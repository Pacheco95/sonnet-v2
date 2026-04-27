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

    // From six cubemap faces. desc.type must be CubeMap and desc.size sets
    // each face's dimensions; all six faces must share the same size and
    // channel layout.
    GlTexture2D(const api::render::TextureDesc &desc,
                const api::render::SamplerDesc &sampler,
                const api::render::CubeMapFaces &faces);

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
    [[nodiscard]] std::uintptr_t getImGuiTextureId()                  override { return static_cast<std::uintptr_t>(m_texture); }

private:
    unsigned                    m_texture    = 0;
    api::render::TextureDesc    m_textureDesc;
    api::render::SamplerDesc    m_samplerDesc;

    // Resolved target — GL_TEXTURE_2D for Texture2D, GL_TEXTURE_CUBE_MAP for
    // CubeMap. Stored so bind()/unbind() target the correct binding point
    // without re-deriving from desc each call.
    unsigned                    m_target     = 0;

    void applySamplerState() const;
};

} // namespace sonnet::renderer::opengl
