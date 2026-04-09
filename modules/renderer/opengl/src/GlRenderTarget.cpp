#include <sonnet/renderer/opengl/GlRenderTarget.h>

#include <cassert>
#include <stdexcept>
#include <variant>

namespace sonnet::renderer::opengl {

using namespace api::render;

GlRenderTarget::GlRenderTarget(const RenderTargetDesc &desc, ITextureFactory &textureFactory)
    : m_width(desc.width), m_height(desc.height) {
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    attachColorTextures(desc, textureFactory);

    if (desc.depth.has_value()) {
        attachDepth(*desc.depth, textureFactory);
    }

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
}

const ITexture *GlRenderTarget::colorTexture(std::size_t index) const {
    assert(index < m_colorTextures.size());
    return m_colorTextures[index].get();
}

const ITexture *GlRenderTarget::depthTexture() const {
    return m_depthTexture.get();
}

void GlRenderTarget::attachColorTextures(const RenderTargetDesc &desc, ITextureFactory &factory) {
    std::vector<GLenum> drawBuffers;

    for (std::size_t i = 0; i < desc.colors.size(); ++i) {
        const auto &att = desc.colors[i];
        TextureDesc texDesc{
            .size        = {m_width, m_height},
            .format      = att.format,
            .type        = TextureType::Texture2D,
            .usageFlags  = static_cast<TextureUsage>(TextureUsage::ColorAttachment | TextureUsage::Sampled),
            .colorSpace  = ColorSpace::Linear,
            .useMipmaps  = false,
        };

        auto tex = factory.create(texDesc, att.samplerDesc);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i),
                               GL_TEXTURE_2D,
                               tex->getNativeHandle(), 0);

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
            .type        = TextureType::Texture2D,
            .usageFlags  = static_cast<TextureUsage>(TextureUsage::DepthAttachment | TextureUsage::Sampled),
            .colorSpace  = ColorSpace::Linear,
            .useMipmaps  = false,
        };

        auto tex = factory.create(texDesc, att.samplerDesc);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                               tex->getNativeHandle(), 0);
        m_depthTexture = std::move(tex);
    } else {
        // RenderBuffer depth
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
