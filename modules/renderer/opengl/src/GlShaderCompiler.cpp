#include <sonnet/renderer/opengl/GlShaderCompiler.h>
#include <sonnet/renderer/opengl/GlShader.h>

#include <glad/glad.h>

#include <stdexcept>
#include <string>

namespace sonnet::renderer::opengl {

static GLuint compileStage(GLenum type, const char *source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        const char *stageName = (type == GL_VERTEX_SHADER) ? "Vertex" : "Fragment";
        throw std::runtime_error(std::string(stageName) + " shader compilation failed: " + log);
    }
    return shader;
}

std::unique_ptr<api::render::IShader> GlShaderCompiler::operator()(
    const std::string &vertexSrc, const std::string &fragmentSrc) const {

    const GLuint vert = compileStage(GL_VERTEX_SHADER,   vertexSrc.c_str());
    const GLuint frag = compileStage(GL_FRAGMENT_SHADER, fragmentSrc.c_str());

    const GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        glDeleteProgram(program);
        throw std::runtime_error(std::string("Shader link failed: ") + log);
    }

    return std::make_unique<GlShader>(program, vertexSrc, fragmentSrc);
}

} // namespace sonnet::renderer::opengl
