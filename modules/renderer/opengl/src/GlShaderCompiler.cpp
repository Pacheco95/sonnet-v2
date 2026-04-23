#include <sonnet/renderer/opengl/GlShaderCompiler.h>
#include <sonnet/renderer/opengl/GlShader.h>

#include <glad/glad.h>

#include <stdexcept>
#include <string>

namespace sonnet::renderer::opengl {

// Preamble injected after #version for every GLSL source the engine compiles
// under OpenGL. Matches VkShaderCompiler's kVulkanPreamble except VULKAN is
// not defined here, and SET() collapses to binding-only so GL accepts it.
// Keeps shader source a single unified tree across both backends.
static constexpr const char *kOpenGLPreamble =
    "#define SET(n,b) binding = b\n";

// Insert `preamble` immediately after the source's #version line. If the
// source has no #version (unusual), prepend the preamble. Strings returned by
// value; caller holds storage for glShaderSource.
static std::string injectPreamble(const std::string &source, const std::string &preamble) {
    const auto versionPos = source.find("#version");
    if (versionPos == std::string::npos) {
        return preamble + source;
    }
    const auto eol = source.find('\n', versionPos);
    if (eol == std::string::npos) {
        return source + "\n" + preamble;
    }
    return source.substr(0, eol + 1) + preamble + source.substr(eol + 1);
}

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

    const std::string vertPatched = injectPreamble(vertexSrc,   kOpenGLPreamble);
    const std::string fragPatched = injectPreamble(fragmentSrc, kOpenGLPreamble);

    const GLuint vert = compileStage(GL_VERTEX_SHADER,   vertPatched.c_str());
    const GLuint frag = compileStage(GL_FRAGMENT_SHADER, fragPatched.c_str());

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
