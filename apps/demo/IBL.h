// IBL.h — Image-Based Lighting pre-computation (startup, raw OpenGL).
//
// Loads an equirectangular HDR map and bakes three GPU resources:
//   • irradianceCube  — 32×32  diffuse irradiance cubemap
//   • prefilteredCube — 128×128 specular pre-filtered cubemap (5 mip levels)
//   • brdfLUT         — 512×512 GGX split-sum BRDF look-up table (RG16F)
//
// The equirectangular texture is also retained for the skybox pass.
// All textures are registered with the engine Renderer so they can be
// used as normal GPUTextureHandles in material instances.
//
// Usage:
//   IBLMaps ibl = buildIBL(renderer, hdrPath, shaderDir);
//   cubeMat.addTexture("uIrradianceMap",  ibl.irradianceHandle);
//   cubeMat.addTexture("uPrefilteredMap", ibl.prefilteredHandle);
//   cubeMat.addTexture("uBRDFLUT",        ibl.brdfLUTHandle);
//   skyMat.addTexture("uEnvMap",          ibl.equirectHandle);

#pragma once

#include <sonnet/api/render/ITexture.h>
#include <sonnet/renderer/frontend/Renderer.h>

#include <glad/glad.h>
#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

// ── ITexture wrappers for raw GL textures ─────────────────────────────────────

class RawGLTexture2D final : public sonnet::api::render::ITexture {
public:
    explicit RawGLTexture2D(GLuint id) : m_id(id) {
        m_texDesc.format = sonnet::api::render::TextureFormat::RGBA16F;
        m_texDesc.type   = sonnet::api::render::TextureType::Texture2D;
    }
    ~RawGLTexture2D() override { /* owned externally by IBLMaps */ }

    void bind(std::uint8_t slot)   const override {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_id);
    }
    void unbind(std::uint8_t slot) const override {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    [[nodiscard]] const sonnet::api::render::TextureDesc &textureDesc() const override { return m_texDesc; }
    [[nodiscard]] const sonnet::api::render::SamplerDesc &samplerDesc() const override { return m_sampDesc; }
    [[nodiscard]] unsigned getNativeHandle()                            const override { return m_id; }

private:
    GLuint                               m_id;
    sonnet::api::render::TextureDesc     m_texDesc{};
    sonnet::api::render::SamplerDesc     m_sampDesc{};
};

class RawGLCubeMap final : public sonnet::api::render::ITexture {
public:
    explicit RawGLCubeMap(GLuint id) : m_id(id) {
        m_texDesc.format = sonnet::api::render::TextureFormat::RGBA16F;
        m_texDesc.type   = sonnet::api::render::TextureType::CubeMap;
    }
    ~RawGLCubeMap() override { /* owned externally by IBLMaps */ }

    void bind(std::uint8_t slot)   const override {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_id);
    }
    void unbind(std::uint8_t slot) const override {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    [[nodiscard]] const sonnet::api::render::TextureDesc &textureDesc() const override { return m_texDesc; }
    [[nodiscard]] const sonnet::api::render::SamplerDesc &samplerDesc() const override { return m_sampDesc; }
    [[nodiscard]] unsigned getNativeHandle()                            const override { return m_id; }

private:
    GLuint                               m_id;
    sonnet::api::render::TextureDesc     m_texDesc{};
    sonnet::api::render::SamplerDesc     m_sampDesc{};
};

// ── Result ────────────────────────────────────────────────────────────────────

struct IBLMaps {
    // Raw GL IDs (owned — call destroyIBL() when done, or let the process exit).
    GLuint equirectTex     = 0; // 2D RGB16F — equirectangular HDR for skybox
    GLuint irradianceCube  = 0; // 32×32 RGB16F cubemap — diffuse irradiance
    GLuint prefilteredCube = 0; // 128×128 RGB16F cubemap (mipped) — specular
    GLuint brdfLUT         = 0; // 512×512 RG16F 2D — split-sum BRDF LUT
    int    prefilteredLODs = 5; // mip levels generated in prefilteredCube

    // Engine handles (registered with Renderer::registerRawTexture).
    sonnet::core::GPUTextureHandle equirectHandle{};
    sonnet::core::GPUTextureHandle irradianceHandle{};
    sonnet::core::GPUTextureHandle prefilteredHandle{};
    sonnet::core::GPUTextureHandle brdfLUTHandle{};
};

// ── Internal helpers ──────────────────────────────────────────────────────────

namespace ibl_detail {

static std::string loadFile(const std::filesystem::path &p) {
    std::ifstream f{p};
    if (!f) throw std::runtime_error("IBL: cannot open '" + p.string() + "'");
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static GLuint compileShader(GLenum type, const std::string &src) {
    GLuint s = glCreateShader(type);
    const char *csrc = src.c_str();
    glShaderSource(s, 1, &csrc, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        glDeleteShader(s);
        throw std::runtime_error(std::string("IBL shader compile error:\n") + log);
    }
    return s;
}

static GLuint linkProgram(GLuint vert, GLuint frag) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        glDeleteProgram(prog);
        throw std::runtime_error(std::string("IBL program link error:\n") + log);
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
}

static GLuint buildProgram(const std::filesystem::path &vertPath,
                            const std::filesystem::path &fragPath) {
    auto vs = compileShader(GL_VERTEX_SHADER,   loadFile(vertPath));
    auto fs = compileShader(GL_FRAGMENT_SHADER, loadFile(fragPath));
    return linkProgram(vs, fs);
}

// Unit cube positions (36 vertices, no indices).
static const float CUBE_VERTS[] = {
    -1,-1,-1,  1,-1,-1,  1, 1,-1,  1, 1,-1, -1, 1,-1, -1,-1,-1, // -Z
    -1,-1, 1,  1,-1, 1,  1, 1, 1,  1, 1, 1, -1, 1, 1, -1,-1, 1, // +Z
    -1, 1, 1, -1, 1,-1, -1,-1,-1, -1,-1,-1, -1,-1, 1, -1, 1, 1, // -X
     1, 1, 1,  1, 1,-1,  1,-1,-1,  1,-1,-1,  1,-1, 1,  1, 1, 1, // +X
    -1,-1,-1,  1,-1,-1,  1,-1, 1,  1,-1, 1, -1,-1, 1, -1,-1,-1, // -Y
    -1, 1,-1,  1, 1,-1,  1, 1, 1,  1, 1, 1, -1, 1, 1, -1, 1,-1  // +Y
};

static GLuint makeCubeVAO() {
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_VERTS), CUBE_VERTS, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glDeleteBuffers(1, &vbo); // vao holds a reference
    return vao;
}

// 6 capture views for a cubemap rendered from the origin.
static std::array<glm::mat4, 6> captureViews() {
    return {
        glm::lookAt(glm::vec3{0}, { 1, 0, 0}, {0,-1, 0}),
        glm::lookAt(glm::vec3{0}, {-1, 0, 0}, {0,-1, 0}),
        glm::lookAt(glm::vec3{0}, { 0, 1, 0}, {0, 0, 1}),
        glm::lookAt(glm::vec3{0}, { 0,-1, 0}, {0, 0,-1}),
        glm::lookAt(glm::vec3{0}, { 0, 0, 1}, {0,-1, 0}),
        glm::lookAt(glm::vec3{0}, { 0, 0,-1}, {0,-1, 0}),
    };
}

static const glm::mat4 CAPTURE_PROJ =
    glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

static void renderCubeToFaces(GLuint prog, GLuint fbo, GLuint cubeVAO,
                               GLuint cubeTex, int size,
                               int mipLevel = 0) {
    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "uProjection"), 1, GL_FALSE,
                       glm::value_ptr(CAPTURE_PROJ));

    const auto views = captureViews();

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, size, size);

    for (int face = 0; face < 6; ++face) {
        glUniformMatrix4fv(glGetUniformLocation(prog, "uView"), 1, GL_FALSE,
                           glm::value_ptr(views[face]));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                               cubeTex, mipLevel);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindVertexArray(cubeVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace ibl_detail

// ── Public API ────────────────────────────────────────────────────────────────

inline IBLMaps buildIBL(sonnet::renderer::frontend::Renderer &renderer,
                         const std::filesystem::path &hdrPath,
                         const std::filesystem::path &shaderDir) {
    using namespace ibl_detail;

    // ── Save GL state ─────────────────────────────────────────────────────────
    GLint prevFBO      = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4]; glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glEnable(GL_DEPTH_TEST);

    // ── Load HDR equirectangular image ────────────────────────────────────────
    stbi_set_flip_vertically_on_load(true);
    int w, h, ch;
    float *data = stbi_loadf(hdrPath.string().c_str(), &w, &h, &ch, 0);
    if (!data) throw std::runtime_error("IBL: failed to load HDR '" + hdrPath.string() + "'");

    GLuint equirectTex;
    glGenTextures(1, &equirectTex);
    glBindTexture(GL_TEXTURE_2D, equirectTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, data);
    stbi_image_free(data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // ── Shared FBO + RBO for capture ──────────────────────────────────────────
    GLuint captureFBO, captureRBO;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);

    GLuint cubeVAO = makeCubeVAO();

    // ── Step 1: env cubemap (equirect → cubemap, 512×512) ────────────────────
    constexpr int ENV_SIZE = 512;
    GLuint envCube;
    glGenTextures(1, &envCube);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);
    for (int f = 0; f < 6; ++f)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0,
                     GL_RGB16F, ENV_SIZE, ENV_SIZE, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, ENV_SIZE, ENV_SIZE);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

    {
        GLuint prog = buildProgram(shaderDir / "ibl/capture.vert",
                                   shaderDir / "ibl/equirect_to_cube.frag");
        glUseProgram(prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, equirectTex);
        glUniform1i(glGetUniformLocation(prog, "uEquirectMap"), 0);
        renderCubeToFaces(prog, captureFBO, cubeVAO, envCube, ENV_SIZE);
        glDeleteProgram(prog);
    }

    glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    // ── Step 2: irradiance cubemap (32×32) ────────────────────────────────────
    constexpr int IRRAD_SIZE = 32;
    GLuint irradianceCube;
    glGenTextures(1, &irradianceCube);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceCube);
    for (int f = 0; f < 6; ++f)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0,
                     GL_RGB16F, IRRAD_SIZE, IRRAD_SIZE, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, IRRAD_SIZE, IRRAD_SIZE);

    {
        GLuint prog = buildProgram(shaderDir / "ibl/capture.vert",
                                   shaderDir / "ibl/irradiance.frag");
        glUseProgram(prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);
        glUniform1i(glGetUniformLocation(prog, "uEnvMap"), 0);
        renderCubeToFaces(prog, captureFBO, cubeVAO, irradianceCube, IRRAD_SIZE);
        glDeleteProgram(prog);
    }

    // ── Step 3: pre-filtered specular cubemap (128×128, 5 mip levels) ─────────
    constexpr int   PREFILTER_SIZE = 128;
    constexpr int   NUM_MIPS       = 5;
    GLuint prefilteredCube;
    glGenTextures(1, &prefilteredCube);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilteredCube);
    for (int f = 0; f < 6; ++f)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0,
                     GL_RGB16F, PREFILTER_SIZE, PREFILTER_SIZE, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP); // allocate mip storage

    {
        GLuint prog = buildProgram(shaderDir / "ibl/capture.vert",
                                   shaderDir / "ibl/prefilter.frag");
        glUseProgram(prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);
        glUniform1i(glGetUniformLocation(prog, "uEnvMap"), 0);

        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
        const auto views = captureViews();
        glUniformMatrix4fv(glGetUniformLocation(prog, "uProjection"), 1, GL_FALSE,
                           glm::value_ptr(CAPTURE_PROJ));

        for (int mip = 0; mip < NUM_MIPS; ++mip) {
            const int mipSize = static_cast<int>(PREFILTER_SIZE * std::pow(0.5, mip));
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipSize, mipSize);
            glViewport(0, 0, mipSize, mipSize);

            const float roughness = static_cast<float>(mip) / static_cast<float>(NUM_MIPS - 1);
            glUniform1f(glGetUniformLocation(prog, "uRoughness"), roughness);

            for (int face = 0; face < 6; ++face) {
                glUniformMatrix4fv(glGetUniformLocation(prog, "uView"), 1, GL_FALSE,
                                   glm::value_ptr(views[face]));
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                       prefilteredCube, mip);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glBindVertexArray(cubeVAO);
                glDrawArrays(GL_TRIANGLES, 0, 36);
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteProgram(prog);
    }

    // ── Step 4: BRDF LUT (512×512 RG16F) ────────────────────────────────────
    constexpr int LUT_SIZE = 512;
    GLuint brdfLUT;
    glGenTextures(1, &brdfLUT);
    glBindTexture(GL_TEXTURE_2D, brdfLUT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, LUT_SIZE, LUT_SIZE, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Fullscreen quad for BRDF LUT.
    const float quadVerts[] = {
        -1,-1,0,  0,0,
         1,-1,0,  1,0,
         1, 1,0,  1,1,
        -1,-1,0,  0,0,
         1, 1,0,  1,1,
        -1, 1,0,  0,1,
    };
    GLuint quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(2); // layout(location=2) = texcoord
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void *>(3 * sizeof(float)));

    {
        GLuint prog = buildProgram(shaderDir / "ibl/brdf_lut.vert",
                                   shaderDir / "ibl/brdf_lut.frag");
        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, LUT_SIZE, LUT_SIZE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, brdfLUT, 0);
        glViewport(0, 0, LUT_SIZE, LUT_SIZE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(prog);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteProgram(prog);
    }

    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteFramebuffers(1, &captureFBO);
    glDeleteRenderbuffers(1, &captureRBO);

    // ── Restore GL state ──────────────────────────────────────────────────────
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (!prevDepth) glDisable(GL_DEPTH_TEST);

    // ── Register with engine renderer ─────────────────────────────────────────
    IBLMaps maps;
    maps.equirectTex     = equirectTex;
    maps.irradianceCube  = irradianceCube;
    maps.prefilteredCube = prefilteredCube;
    maps.brdfLUT         = brdfLUT;
    maps.prefilteredLODs = NUM_MIPS;

    maps.equirectHandle  = renderer.registerRawTexture(std::make_unique<RawGLTexture2D>(equirectTex));
    maps.irradianceHandle= renderer.registerRawTexture(std::make_unique<RawGLCubeMap>(irradianceCube));
    maps.prefilteredHandle=renderer.registerRawTexture(std::make_unique<RawGLCubeMap>(prefilteredCube));
    maps.brdfLUTHandle   = renderer.registerRawTexture(std::make_unique<RawGLTexture2D>(brdfLUT));

    return maps;
}
