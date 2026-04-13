#include <sonnet/renderer/opengl/GlShader.h>

#include <vector>

namespace sonnet::renderer::opengl {

GlShader::GlShader(GLuint program, std::string vertexSrc, std::string fragmentSrc)
    : m_program(program)
    , m_vertexSrc(std::move(vertexSrc))
    , m_fragmentSrc(std::move(fragmentSrc)) {
    resolveUniforms();
}

GlShader::~GlShader() {
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

void GlShader::resolveUniforms() {
    GLint count = 0;
    glGetProgramiv(m_program, GL_ACTIVE_UNIFORMS, &count);

    GLint maxLen = 0;
    glGetProgramiv(m_program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxLen);

    std::vector<GLchar> nameBuf(static_cast<std::size_t>(maxLen));
    m_uniforms.reserve(static_cast<std::size_t>(count));

    for (GLuint i = 0; i < static_cast<GLuint>(count); ++i) {
        GLsizei length = 0;
        GLint   size   = 0;
        GLenum  glType = 0;
        glGetActiveUniform(m_program, i, maxLen, &length, &size, &glType, nameBuf.data());

        std::string name(nameBuf.data(), static_cast<std::size_t>(length));

        core::UniformType type = core::UniformType::Float;
        if      (glType == GL_INT)        type = core::UniformType::Int;
        else if (glType == GL_FLOAT)      type = core::UniformType::Float;
        else if (glType == GL_FLOAT_VEC2) type = core::UniformType::Vec2;
        else if (glType == GL_FLOAT_VEC3) type = core::UniformType::Vec3;
        else if (glType == GL_FLOAT_VEC4) type = core::UniformType::Vec4;
        else if (glType == GL_FLOAT_MAT4) type = core::UniformType::Mat4;
        else if (glType == GL_SAMPLER_2D)        type = core::UniformType::Sampler;
        else if (glType == GL_SAMPLER_CUBE)      type = core::UniformType::Sampler;
        else if (glType == GL_SAMPLER_2D_SHADOW) type = core::UniformType::Sampler;
        // Unknown uniform types are silently skipped.

        if (size > 1) {
            // Array uniform — GL appends "[0]" to the name. Strip it and register
            // each element individually so per-index uploads (e.g. uKernel[i]) work.
            std::string base = name;
            if (base.size() > 3 && base.substr(base.size() - 3) == "[0]")
                base.resize(base.size() - 3);
            for (GLint j = 0; j < size; ++j) {
                const std::string elemName = base + "[" + std::to_string(j) + "]";
                const GLint elemLoc = glGetUniformLocation(m_program, elemName.c_str());
                if (elemLoc != -1)
                    m_uniforms.emplace(elemName, core::UniformDescriptor{type, elemLoc});
            }
        } else {
            const GLint location = glGetUniformLocation(m_program, name.c_str());
            m_uniforms.emplace(std::move(name), core::UniformDescriptor{type, location});
        }
    }
}

} // namespace sonnet::renderer::opengl
