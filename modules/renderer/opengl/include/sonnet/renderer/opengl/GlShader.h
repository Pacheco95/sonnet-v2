#pragma once

#include <sonnet/api/render/IShader.h>
#include <sonnet/core/Macros.h>

#include <glad/glad.h>

#include <string>

namespace sonnet::renderer::opengl {

class GlShader final : public api::render::IShader {
public:
    GlShader(GLuint program, std::string vertexSrc, std::string fragmentSrc);
    ~GlShader() override;

    SN_NON_COPYABLE(GlShader);
    SN_NON_MOVABLE(GlShader);

    [[nodiscard]] const std::string              &getVertexSource()   const override { return m_vertexSrc; }
    [[nodiscard]] const std::string              &getFragmentSource() const override { return m_fragmentSrc; }
    [[nodiscard]] const core::ShaderProgram      &getProgram()        const override { return m_program; }
    [[nodiscard]] const core::UniformDescriptorMap &getUniforms()     const override { return m_uniforms; }

    void bind()   const override { glUseProgram(m_program); }
    void unbind() const override { glUseProgram(0); }

private:
    GLuint                     m_program;
    std::string                m_vertexSrc;
    std::string                m_fragmentSrc;
    core::UniformDescriptorMap m_uniforms;

    void resolveUniforms();
};

} // namespace sonnet::renderer::opengl
