#pragma once

#include <sonnet/api/render/IGpuBuffer.h>
#include <sonnet/core/Macros.h>

#include <glad/glad.h>

#include <cassert>
#include <cstddef>
#include <stdexcept>

namespace sonnet::renderer::opengl {

[[nodiscard]] constexpr GLenum toGlBufferTarget(api::render::BufferType type) {
    switch (type) {
        case api::render::BufferType::Vertex:  return GL_ARRAY_BUFFER;
        case api::render::BufferType::Index:   return GL_ELEMENT_ARRAY_BUFFER;
        case api::render::BufferType::Uniform: return GL_UNIFORM_BUFFER;
    }
    return 0;
}

class GlGpuBuffer final : public api::render::IGpuBuffer {
public:
    GlGpuBuffer(api::render::BufferType type, std::size_t size, const void *data)
        : m_glTarget(toGlBufferTarget(type)) {
        glGenBuffers(1, &m_handle);
        if (!m_handle) throw std::runtime_error("glGenBuffers failed");
        bind();
        const GLenum usage = (type == api::render::BufferType::Uniform)
                             ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
        glBufferData(m_glTarget, static_cast<GLsizeiptr>(size), data, usage);
    }

    ~GlGpuBuffer() override {
        if (m_handle) glDeleteBuffers(1, &m_handle);
    }

    SN_NON_COPYABLE(GlGpuBuffer);
    SN_NON_MOVABLE(GlGpuBuffer);

    void bind() const override {
        assert(m_handle);
        glBindBuffer(m_glTarget, m_handle);
    }

    void update(const void *data, std::size_t size) override {
        assert(m_handle);
        bind();
        glBufferSubData(m_glTarget, 0, static_cast<GLsizeiptr>(size), data);
    }

    void bindBase(std::uint32_t bindingPoint) const override {
        assert(m_handle);
        glBindBufferBase(m_glTarget, bindingPoint, m_handle);
    }

    [[nodiscard]] GLuint handle() const { return m_handle; }

private:
    GLuint m_handle  = 0;
    GLenum m_glTarget;
};

} // namespace sonnet::renderer::opengl
