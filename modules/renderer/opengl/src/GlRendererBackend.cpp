#include <sonnet/renderer/opengl/GlRendererBackend.h>
#include <sonnet/renderer/opengl/GlGpuBuffer.h>
#include <sonnet/renderer/opengl/GlVertexInputState.h>

#include "GlUtils.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <cassert>
#include <stdexcept>

namespace sonnet::renderer::opengl {

GlRendererBackend::GlRendererBackend() = default;

void GlRendererBackend::initialize() {
    if (!gladLoadGL()) {
        throw std::runtime_error("GlRendererBackend: gladLoadGL failed");
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glDepthFunc(GL_LESS);
    glEnable(GL_FRAMEBUFFER_SRGB);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
}

void GlRendererBackend::beginFrame() {}
void GlRendererBackend::endFrame()   {}

void GlRendererBackend::clear(const api::render::ClearOptions &options) {
    for (const auto &c : options.colors) {
        glClearBufferfv(GL_COLOR, static_cast<GLint>(c.attachmentIndex), glm::value_ptr(c.value));
    }
    if (options.depth) {
        // glClearBufferfv respects GL_DEPTH_WRITEMASK; force it on so the
        // clear is never silently suppressed by a previous depthWrite=false pass.
        glDepthMask(GL_TRUE);
        const float d = *options.depth;
        glClearBufferfv(GL_DEPTH, 0, &d);
    }
    if (options.stencil) {
        const GLint s = static_cast<GLint>(*options.stencil);
        glClearBufferiv(GL_STENCIL, 0, &s);
    }
}

void GlRendererBackend::bindDefaultRenderTarget() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GlRendererBackend::bindRenderTarget(const api::render::IRenderTarget &target) {
    target.bind();
}

void GlRendererBackend::setViewport(std::uint32_t width, std::uint32_t height) {
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
}

void GlRendererBackend::setFillMode(api::render::FillMode mode) {
    glPolygonMode(GL_FRONT_AND_BACK,
                  mode == api::render::FillMode::Wireframe ? GL_LINE : GL_FILL);
}

void GlRendererBackend::setDepthTest(bool enabled) {
    enabled ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
}

void GlRendererBackend::setDepthWrite(bool enabled) {
    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
}

void GlRendererBackend::setDepthFunc(api::render::DepthFunction func) {
    switch (func) {
        case api::render::DepthFunction::Less:      glDepthFunc(GL_LESS);   break;
        case api::render::DepthFunction::LessEqual: glDepthFunc(GL_LEQUAL); break;
    }
}

void GlRendererBackend::setCull(api::render::CullMode mode) {
    if (mode == api::render::CullMode::None) {
        glDisable(GL_CULL_FACE);
        return;
    }
    glEnable(GL_CULL_FACE);
    glCullFace(mode == api::render::CullMode::Back ? GL_BACK : GL_FRONT);
}

void GlRendererBackend::setBlend(bool enabled) {
    enabled ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
}

void GlRendererBackend::setBlendFunc(api::render::BlendFactor src, api::render::BlendFactor dst) {
    glBlendFunc(toGlBlendFactor(src), toGlBlendFactor(dst));
}

void GlRendererBackend::setSRGB(bool enabled) {
    enabled ? glEnable(GL_FRAMEBUFFER_SRGB) : glDisable(GL_FRAMEBUFFER_SRGB);
}

std::unique_ptr<api::render::IGpuBuffer> GlRendererBackend::createBuffer(
    api::render::BufferType type, const void *data, std::size_t size) {
    return std::make_unique<GlGpuBuffer>(type, size, data);
}

std::unique_ptr<api::render::IVertexInputState> GlRendererBackend::createVertexInputState(
    const api::render::VertexLayout &layout,
    const api::render::IGpuBuffer   &vertexBuffer,
    const api::render::IGpuBuffer   &indexBuffer) {

    auto vao = std::make_unique<GlVertexInputState>(); // binds VAO
    vertexBuffer.bind();
    indexBuffer.bind();
    setupVertexAttributes(layout);
    return vao;
}

void GlRendererBackend::setUniform(UniformLocation location, const core::UniformValue &value) {
    std::visit([&](const auto &v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int>) {
            glUniform1i(location, v);
        } else if constexpr (std::is_same_v<T, float>) {
            glUniform1f(location, v);
        } else if constexpr (std::is_same_v<T, glm::vec2>) {
            glUniform2fv(location, 1, glm::value_ptr(v));
        } else if constexpr (std::is_same_v<T, glm::vec3>) {
            glUniform3fv(location, 1, glm::value_ptr(v));
        } else if constexpr (std::is_same_v<T, glm::vec4>) {
            glUniform4fv(location, 1, glm::value_ptr(v));
        } else if constexpr (std::is_same_v<T, glm::mat4>) {
            glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(v));
        } else if constexpr (std::is_same_v<T, core::Sampler>) {
            glUniform1i(location, static_cast<GLint>(v));
        }
    }, value);
}

void GlRendererBackend::drawIndexed(std::size_t indexCount) {
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, nullptr);
}

void GlRendererBackend::setupVertexAttributes(const api::render::VertexLayout &layout) {
    const std::size_t stride = layout.getStride();
    std::size_t offset = 0;

    for (const auto &attr : layout.getAttributes()) {
        std::visit([&](const auto &a) {
            using A = std::decay_t<decltype(a)>;
            glEnableVertexAttribArray(A::location);
            glVertexAttribPointer(
                A::location,
                static_cast<GLint>(A::componentCount),
                GL_FLOAT,
                a.normalize ? GL_TRUE : GL_FALSE,
                static_cast<GLsizei>(stride),
                reinterpret_cast<const void *>(offset));
            offset += A::sizeInBytes;
        }, attr);
    }
}

} // namespace sonnet::renderer::opengl
