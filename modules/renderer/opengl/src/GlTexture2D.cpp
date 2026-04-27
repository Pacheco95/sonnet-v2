#include <sonnet/renderer/opengl/GlTexture2D.h>

#include "GlUtils.h"

#include <cassert>
#include <stdexcept>

namespace sonnet::renderer::opengl {

GlTexture2D::GlTexture2D(const api::render::TextureDesc &desc,
                         const api::render::SamplerDesc &sampler,
                         const api::render::CPUTextureBuffer &data)
    : m_textureDesc(desc), m_samplerDesc(sampler), m_target(GL_TEXTURE_2D) {
    if (data.texels.empty()) throw std::runtime_error("GlTexture2D: empty texel data");

    glGenTextures(1, &m_texture);
    if (!m_texture) throw std::runtime_error("glGenTextures failed");

    glBindTexture(m_target, m_texture);

    GLint prevAlign = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    const auto fmt = getGlTextureFormat(desc);
    glTexImage2D(m_target, 0,
                 fmt.internalFormat,
                 static_cast<GLsizei>(desc.size.x),
                 static_cast<GLsizei>(desc.size.y),
                 0, fmt.format, fmt.type,
                 data.texels.data());

    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);

    applySamplerState();
    glBindTexture(m_target, 0);
}

GlTexture2D::GlTexture2D(const api::render::TextureDesc &desc,
                         const api::render::SamplerDesc &sampler,
                         const api::render::CubeMapFaces &faces)
    : m_textureDesc(desc), m_samplerDesc(sampler), m_target(GL_TEXTURE_CUBE_MAP) {
    if (desc.type != api::render::TextureType::CubeMap) {
        throw std::runtime_error("GlTexture2D: cubemap ctor requires desc.type == CubeMap");
    }

    glGenTextures(1, &m_texture);
    if (!m_texture) throw std::runtime_error("glGenTextures failed");

    glBindTexture(m_target, m_texture);

    GLint prevAlign = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    const auto fmt = getGlTextureFormat(desc);

    // CubeMapFaces enumerates faces in OpenGL's standard order: positive-X,
    // negative-X, positive-Y, negative-Y, positive-Z, negative-Z. The struct
    // names them right/left/top/bottom/front/back to match Sonnet's coordinate
    // convention, but the GL constants below assume the same enumeration.
    const core::Texels *facePtrs[6] = {
        &faces.right, &faces.left, &faces.top, &faces.bottom, &faces.front, &faces.back,
    };
    for (std::uint32_t f = 0; f < 6; ++f) {
        const auto &texels = *facePtrs[f];
        if (texels.empty()) {
            throw std::runtime_error("GlTexture2D: cubemap face is empty");
        }
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0,
                     fmt.internalFormat,
                     static_cast<GLsizei>(desc.size.x),
                     static_cast<GLsizei>(desc.size.y),
                     0, fmt.format, fmt.type, texels.data());
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);

    applySamplerState();
    glBindTexture(m_target, 0);
}

GlTexture2D::GlTexture2D(const api::render::TextureDesc &desc,
                         const api::render::SamplerDesc &sampler)
    : m_textureDesc(desc), m_samplerDesc(sampler),
      m_target(desc.type == api::render::TextureType::CubeMap
                   ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D) {
    glGenTextures(1, &m_texture);
    if (!m_texture) throw std::runtime_error("glGenTextures failed");

    glBindTexture(m_target, m_texture);

    // Explicit mip-level count for RTs that need a chain (IBL prefilter
    // cubemap = 5 mips). 0 means single mip — matches prior behavior.
    const GLsizei mipLevels =
        desc.mipLevels > 0 ? static_cast<GLsizei>(desc.mipLevels) : 1;

    const auto fmt = getGlTextureFormat(desc);
    if (m_target == GL_TEXTURE_CUBE_MAP) {
        for (std::uint32_t f = 0; f < 6; ++f) {
            for (GLsizei mip = 0; mip < mipLevels; ++mip) {
                const GLsizei mipW = std::max(1, static_cast<GLsizei>(desc.size.x) >> mip);
                const GLsizei mipH = std::max(1, static_cast<GLsizei>(desc.size.y) >> mip);
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, mip,
                             fmt.internalFormat, mipW, mipH,
                             0, fmt.format, fmt.type, nullptr);
            }
        }
    } else {
        for (GLsizei mip = 0; mip < mipLevels; ++mip) {
            const GLsizei mipW = std::max(1, static_cast<GLsizei>(desc.size.x) >> mip);
            const GLsizei mipH = std::max(1, static_cast<GLsizei>(desc.size.y) >> mip);
            glTexImage2D(m_target, mip,
                         fmt.internalFormat, mipW, mipH,
                         0, fmt.format, fmt.type, nullptr);
        }
    }

    // Cap MAX_LEVEL so sampler filtering doesn't try to read levels we
    // didn't allocate. applySamplerState will overwrite this only when
    // requiresMipmaps() AND the texture has CPU data (calls glGenerateMipmap).
    // For RT cubemaps we render to mips manually, so set the cap explicitly.
    glTexParameteri(m_target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(m_target, GL_TEXTURE_MAX_LEVEL, mipLevels - 1);

    applySamplerState();
    glBindTexture(m_target, 0);
}

GlTexture2D::~GlTexture2D() {
    if (m_texture) glDeleteTextures(1, &m_texture);
}

void GlTexture2D::bind(std::uint8_t slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(m_target, m_texture);
}

void GlTexture2D::unbind(std::uint8_t slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(m_target, 0);
}

void GlTexture2D::applySamplerState() const {
    if (m_samplerDesc.requiresMipmaps()) {
        glGenerateMipmap(m_target);
    } else {
        glTexParameteri(m_target, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(m_target, GL_TEXTURE_MAX_LEVEL, 0);
    }

    glTexParameteri(m_target, GL_TEXTURE_WRAP_S,     static_cast<GLint>(toGlWrap(m_samplerDesc.wrapS)));
    glTexParameteri(m_target, GL_TEXTURE_WRAP_T,     static_cast<GLint>(toGlWrap(m_samplerDesc.wrapT)));
    if (m_target == GL_TEXTURE_CUBE_MAP) {
        // Cubemaps need WRAP_R as well; 2D textures don't have a third axis.
        glTexParameteri(m_target, GL_TEXTURE_WRAP_R, static_cast<GLint>(toGlWrap(m_samplerDesc.wrapR)));
    }
    glTexParameteri(m_target, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(toGlMinFilter(m_samplerDesc.minFilter)));
    glTexParameteri(m_target, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(toGlMagFilter(m_samplerDesc.magFilter)));

    if (m_samplerDesc.depthCompare) {
        glTexParameteri(m_target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(m_target, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }
}

} // namespace sonnet::renderer::opengl
