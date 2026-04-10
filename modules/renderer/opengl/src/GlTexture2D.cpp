#include <sonnet/renderer/opengl/GlTexture2D.h>

#include "GlUtils.h"

#include <cassert>
#include <stdexcept>

namespace sonnet::renderer::opengl {

GlTexture2D::GlTexture2D(const api::render::TextureDesc &desc,
                         const api::render::SamplerDesc &sampler,
                         const api::render::CPUTextureBuffer &data)
    : m_textureDesc(desc), m_samplerDesc(sampler) {
    if (data.texels.empty()) throw std::runtime_error("GlTexture2D: empty texel data");

    glGenTextures(1, &m_texture);
    if (!m_texture) throw std::runtime_error("glGenTextures failed");

    glBindTexture(GL_TEXTURE_2D, m_texture);

    GLint prevAlign = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    const auto fmt = getGlTextureFormat(desc);
    glTexImage2D(GL_TEXTURE_2D, 0,
                 fmt.internalFormat,
                 static_cast<GLsizei>(desc.size.x),
                 static_cast<GLsizei>(desc.size.y),
                 0, fmt.format, fmt.type,
                 data.texels.data());

    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);

    applySamplerState();
    glBindTexture(GL_TEXTURE_2D, 0);
}

GlTexture2D::GlTexture2D(const api::render::TextureDesc &desc,
                         const api::render::SamplerDesc &sampler)
    : m_textureDesc(desc), m_samplerDesc(sampler) {
    glGenTextures(1, &m_texture);
    if (!m_texture) throw std::runtime_error("glGenTextures failed");

    glBindTexture(GL_TEXTURE_2D, m_texture);

    const auto fmt = getGlTextureFormat(desc);
    glTexImage2D(GL_TEXTURE_2D, 0,
                 fmt.internalFormat,
                 static_cast<GLsizei>(desc.size.x),
                 static_cast<GLsizei>(desc.size.y),
                 0, fmt.format, fmt.type, nullptr);

    applySamplerState();
    glBindTexture(GL_TEXTURE_2D, 0);
}

GlTexture2D::~GlTexture2D() {
    if (m_texture) glDeleteTextures(1, &m_texture);
}

void GlTexture2D::bind(std::uint8_t slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_texture);
}

void GlTexture2D::unbind(std::uint8_t slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GlTexture2D::applySamplerState() const {
    if (m_samplerDesc.requiresMipmaps()) {
        glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     static_cast<GLint>(toGlWrap(m_samplerDesc.wrapS)));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     static_cast<GLint>(toGlWrap(m_samplerDesc.wrapT)));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(toGlMinFilter(m_samplerDesc.minFilter)));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(toGlMagFilter(m_samplerDesc.magFilter)));

    if (m_samplerDesc.depthCompare) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }
}

} // namespace sonnet::renderer::opengl
