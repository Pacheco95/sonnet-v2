#include <sonnet/renderer/opengl/GlRenderTarget.h>

#include <glad/glad.h>

#include <cassert>
#include <stdexcept>
#include <variant>

namespace sonnet::renderer::opengl {

using namespace api::render;

GlRenderTarget::GlRenderTarget(const RenderTargetDesc &desc, ITextureFactory &textureFactory)
    : m_width(desc.width), m_height(desc.height),
      m_isCubemap(desc.isCubemap),
      m_mipLevels(desc.isCubemap ? std::max(1u, desc.mipLevels) : 1u) {
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    attachColorTextures(desc, textureFactory);

    if (desc.depth.has_value()) {
        attachDepth(*desc.depth, textureFactory);
    }

    // For cubemap RTs, framebuffer completeness needs at least one face
    // attached; the constructor leaves face=0,mip=0 attached (via the loop in
    // attachColorTextures/attachDepth). Subsequent selectCubemapFace calls
    // mutate the FBO at bind() time.
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("GlRenderTarget: incomplete framebuffer");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GlRenderTarget::~GlRenderTarget() {
    m_colorTextures.clear();
    m_depthTexture.reset();
    if (m_depthRbo) glDeleteRenderbuffers(1, &m_depthRbo);
    if (m_fbo)      glDeleteFramebuffers(1, &m_fbo);
}

void GlRenderTarget::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    // Cubemap RT: re-attach the active face/mip before each bind. Color is
    // attached unconditionally (always present for IBL bakes); depth is
    // attached only when it's a cubemap texture (point shadows). A 2D depth
    // RBO stays attached at construction time.
    if (m_isCubemap) {
        if (!m_colorTextures.empty()) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + m_activeFace,
                                   m_colorTextures[0]->getNativeHandle(),
                                   static_cast<GLint>(m_activeMip));
        }
        if (m_depthTexture &&
            m_depthTexture->textureDesc().type == TextureType::CubeMap) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + m_activeFace,
                                   m_depthTexture->getNativeHandle(),
                                   static_cast<GLint>(m_activeMip));
        }
    }
}

std::uint32_t GlRenderTarget::width() const {
    if (!m_isCubemap) return m_width;
    return std::max(1u, m_width >> m_activeMip);
}

std::uint32_t GlRenderTarget::height() const {
    if (!m_isCubemap) return m_height;
    return std::max(1u, m_height >> m_activeMip);
}

void GlRenderTarget::selectCubemapFace(std::uint32_t face, std::uint32_t mipLevel) {
    if (!m_isCubemap) {
        throw std::runtime_error("GlRenderTarget::selectCubemapFace called on non-cubemap RT");
    }
    if (face >= 6) throw std::runtime_error("GlRenderTarget::selectCubemapFace: face >= 6");
    if (mipLevel >= m_mipLevels) {
        throw std::runtime_error("GlRenderTarget::selectCubemapFace: mipLevel >= mipLevels");
    }
    m_activeFace = face;
    m_activeMip  = mipLevel;
}

const ITexture *GlRenderTarget::colorTexture(std::size_t index) const {
    assert(index < m_colorTextures.size());
    return m_colorTextures[index].get();
}

const ITexture *GlRenderTarget::depthTexture() const {
    return m_depthTexture.get();
}

std::array<std::uint8_t, 4> GlRenderTarget::readPixelRGBA8(
    std::uint32_t attachmentIndex, std::uint32_t x, std::uint32_t y) const {
    if (attachmentIndex >= m_colorTextures.size()) {
        throw std::runtime_error("GlRenderTarget::readPixelRGBA8: attachment out of range");
    }

    std::array<std::uint8_t, 4> pixel{};

    // Remember current bindings so callers don't have to reset the read/draw
    // framebuffer after picking.
    GLint prevReadFbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
    glReadPixels(static_cast<GLint>(x), static_cast<GLint>(y), 1, 1,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));

    return pixel;
}

void GlRenderTarget::attachColorTextures(const RenderTargetDesc &desc, ITextureFactory &factory) {
    std::vector<GLenum> drawBuffers;

    for (std::size_t i = 0; i < desc.colors.size(); ++i) {
        const auto &att = desc.colors[i];
        TextureDesc texDesc{
            .size        = {m_width, m_height},
            .format      = att.format,
            .type        = m_isCubemap ? TextureType::CubeMap : TextureType::Texture2D,
            .usageFlags  = static_cast<TextureUsage>(TextureUsage::ColorAttachment | TextureUsage::Sampled),
            .colorSpace  = ColorSpace::Linear,
            .useMipmaps  = false,
            .mipLevels   = m_mipLevels,
        };

        auto tex = factory.create(texDesc, att.samplerDesc);
        if (m_isCubemap) {
            // Cubemap: attach face 0, mip 0 initially. selectCubemapFace
            // changes the binding before subsequent draws.
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i),
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                                   tex->getNativeHandle(), 0);
        } else {
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i),
                                   GL_TEXTURE_2D,
                                   tex->getNativeHandle(), 0);
        }

        drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i));
        m_colorTextures.push_back(std::move(tex));
    }

    if (!drawBuffers.empty()) {
        glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
    } else {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
}

void GlRenderTarget::attachDepth(const DepthAttachmentDesc &depthDesc, ITextureFactory &factory) {
    if (std::holds_alternative<TextureAttachmentDesc>(depthDesc)) {
        const auto &att = std::get<TextureAttachmentDesc>(depthDesc);
        TextureDesc texDesc{
            .size        = {m_width, m_height},
            .format      = TextureFormat::Depth24,
            .type        = m_isCubemap ? TextureType::CubeMap : TextureType::Texture2D,
            .usageFlags  = static_cast<TextureUsage>(TextureUsage::DepthAttachment | TextureUsage::Sampled),
            .colorSpace  = ColorSpace::Linear,
            .useMipmaps  = false,
            .mipLevels   = m_mipLevels,
        };

        auto tex = factory.create(texDesc, att.samplerDesc);
        if (m_isCubemap) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                                   tex->getNativeHandle(), 0);
        } else {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                   tex->getNativeHandle(), 0);
        }
        m_depthTexture = std::move(tex);
    } else {
        // RenderBuffer depth — sized for the largest mip and reused across
        // faces. Smaller mips can scissor-write within the larger renderbuffer.
        glGenRenderbuffers(1, &m_depthRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depthRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                              static_cast<GLsizei>(m_width),
                              static_cast<GLsizei>(m_height));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }
}

} // namespace sonnet::renderer::opengl
