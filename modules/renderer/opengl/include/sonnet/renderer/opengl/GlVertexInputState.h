#pragma once

#include <sonnet/api/render/IVertexInputState.h>
#include <sonnet/core/Macros.h>

#include <glad/glad.h>

#include <cassert>
#include <stdexcept>

namespace sonnet::renderer::opengl {

class GlVertexInputState final : public api::render::IVertexInputState {
public:
    GlVertexInputState() {
        glGenVertexArrays(1, &m_vao);
        if (!m_vao) throw std::runtime_error("glGenVertexArrays failed");
        bind();
    }

    ~GlVertexInputState() override {
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
    }

    SN_NON_COPYABLE(GlVertexInputState);
    SN_NON_MOVABLE(GlVertexInputState);

    void bind()   const override { assert(m_vao); glBindVertexArray(m_vao); }
    void unbind() const override { glBindVertexArray(0); }

    [[nodiscard]] GLuint vao() const { return m_vao; }

private:
    GLuint m_vao = 0;
};

} // namespace sonnet::renderer::opengl
