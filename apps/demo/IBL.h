// IBL.h — Image-Based Lighting pre-computation through the engine.
//
// Loads an equirectangular HDR map and bakes four GPU resources, all owned
// by the frontend Renderer (no raw GL):
//   • equirect       — 2D HDR equirectangular (skybox source)
//   • irradianceCube — 32×32  diffuse irradiance cubemap
//   • prefilteredCube — 128×128 specular pre-filtered cubemap (5 mip levels)
//   • brdfLUT        — 512×512 GGX split-sum BRDF look-up table (RG16F)
//
// All four are returned as `core::GPUTextureHandle`s for use in material
// instances (see PostProcess::buildMaterials).
//
// Usage:
//   IBLMaps ibl = buildIBL(renderer, hdrPath, shaderDir);
//   cubeMat.addTexture("uIrradianceMap",  ibl.irradianceHandle);
//   cubeMat.addTexture("uPrefilteredMap", ibl.prefilteredHandle);
//   cubeMat.addTexture("uBRDFLUT",        ibl.brdfLUTHandle);
//   skyMat.addTexture("uEnvMap",          ibl.equirectHandle);

#pragma once

#include <sonnet/api/render/CPUMesh.h>
#include <sonnet/api/render/FrameContext.h>
#include <sonnet/api/render/Material.h>
#include <sonnet/api/render/RenderItem.h>
#include <sonnet/api/render/VertexLayout.h>
#include <sonnet/primitives/MeshPrimitives.h>
#include <sonnet/renderer/frontend/Renderer.h>

#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// ── Result ────────────────────────────────────────────────────────────────────

struct IBLMaps {
    sonnet::core::GPUTextureHandle equirectHandle{};
    sonnet::core::GPUTextureHandle irradianceHandle{};
    sonnet::core::GPUTextureHandle prefilteredHandle{};
    sonnet::core::GPUTextureHandle brdfLUTHandle{};
    int                            prefilteredLODs = 5;
};

// ── Internal helpers ──────────────────────────────────────────────────────────

namespace ibl_detail {

inline std::string loadFile(const std::filesystem::path &p) {
    std::ifstream f{p};
    if (!f) throw std::runtime_error("IBL: cannot open '" + p.string() + "'");
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// 90° fov perspective for cubemap face rendering. Same on both backends —
// the engine's projection helper handles clip-space Y / NDC Z corrections
// against the active backend's traits.
inline glm::mat4 captureProj() {
    return sonnet::core::projection::perspective(
        glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
}

// 6 capture views from the origin, one per cubemap face. Order matches
// CubeMapFaces / GL_TEXTURE_CUBE_MAP_POSITIVE_X+i: +X, -X, +Y, -Y, +Z, -Z.
inline std::array<glm::mat4, 6> captureViews() {
    return {
        glm::lookAt(glm::vec3{0}, { 1, 0, 0}, {0,-1, 0}),
        glm::lookAt(glm::vec3{0}, {-1, 0, 0}, {0,-1, 0}),
        glm::lookAt(glm::vec3{0}, { 0, 1, 0}, {0, 0, 1}),
        glm::lookAt(glm::vec3{0}, { 0,-1, 0}, {0, 0,-1}),
        glm::lookAt(glm::vec3{0}, { 0, 0, 1}, {0,-1, 0}),
        glm::lookAt(glm::vec3{0}, { 0, 0,-1}, {0,-1, 0}),
    };
}

// Render a single cubemap face: bind the RT face, set viewport, clear, draw.
// Frame must be active before bindRenderTarget/setViewport/clear: the Vulkan
// backend gates those on a pending command buffer. Wasteful on Vulkan (each
// face presents the swapchain) but functionally correct for a startup-time
// precompute. A future Phase 8 could add a one-shot path.
inline void renderFace(sonnet::renderer::frontend::Renderer  &renderer,
                       sonnet::api::render::IRendererBackend &backend,
                       sonnet::core::RenderTargetHandle      rt,
                       std::uint32_t                          face,
                       std::uint32_t                          mipLevel,
                       std::uint32_t                          mipSize,
                       const glm::mat4                       &view,
                       const glm::mat4                       &proj,
                       std::vector<sonnet::api::render::RenderItem> &queue) {
    renderer.selectCubemapFace(rt, face, mipLevel);

    sonnet::api::render::FrameContext ctx{
        .viewMatrix       = view,
        .projectionMatrix = proj,
        .viewPosition     = glm::vec3{0.0f},
        .viewportWidth    = mipSize,
        .viewportHeight   = mipSize,
        .deltaTime        = 0.0f,
    };
    renderer.beginFrame();
    renderer.bindRenderTarget(rt);
    backend.setViewport(mipSize, mipSize);
    backend.setDepthTest(true);
    backend.setDepthWrite(true);
    backend.clear({
        .colors = {{0, glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}}},
        .depth  = 1.0f,
    });
    renderer.render(ctx, queue);
    renderer.endFrame();
}

} // namespace ibl_detail

// ── Public API ────────────────────────────────────────────────────────────────

inline IBLMaps buildIBL(sonnet::renderer::frontend::Renderer  &renderer,
                         sonnet::api::render::IRendererBackend &backend,
                         const std::filesystem::path           &hdrPath,
                         const std::filesystem::path           &shaderDir) {
    using namespace sonnet::api::render;
    using namespace ibl_detail;

    constexpr int ENV_SIZE       = 512;
    constexpr int IRRAD_SIZE     = 32;
    constexpr int PREFILTER_SIZE = 128;
    constexpr int LUT_SIZE       = 512;
    constexpr int NUM_MIPS       = 5;

    const glm::mat4 PROJ  = captureProj();
    const auto      VIEWS = captureViews();
    (void)backend; // backend reference is consumed inside renderFace().

    // ── Load HDR equirectangular image ────────────────────────────────────────
    // Force 4 channels so the byte buffer maps cleanly to RGBA32F. The engine
    // does not natively own a float→half conversion, so we accept the 2× memory
    // cost for the equirect (transient: only sampled once during the env-cube
    // capture). 4096×2048 RGBA32F is ~128 MB but freed shortly after.
    stbi_set_flip_vertically_on_load(true);
    int w = 0, h = 0, ch = 0;
    float *raw = stbi_loadf(hdrPath.string().c_str(), &w, &h, &ch, 4);
    if (!raw) throw std::runtime_error("IBL: failed to load HDR '" + hdrPath.string() + "'");
    const std::size_t equirectFloats = static_cast<std::size_t>(w) * h * 4;
    const std::size_t equirectBytes  = equirectFloats * sizeof(float);

    std::vector<std::byte> equirectBytesVec(equirectBytes);
    std::memcpy(equirectBytesVec.data(), raw, equirectBytes);
    stbi_image_free(raw);

    CPUTextureBuffer equirectData{
        .width    = static_cast<std::uint32_t>(w),
        .height   = static_cast<std::uint32_t>(h),
        .channels = 4,
        .texels   = sonnet::core::Texels(std::move(equirectBytesVec)),
    };

    const auto equirectHandle = renderer.createTexture(
        TextureDesc{
            .size       = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)},
            .format     = TextureFormat::RGBA32F,
            .type       = TextureType::Texture2D,
            .usageFlags = Sampled,
            .colorSpace = ColorSpace::Linear,
            .useMipmaps = false,
        },
        SamplerDesc{
            .minFilter = MinFilter::Linear,
            .magFilter = MagFilter::Linear,
            .wrapS     = TextureWrap::ClampToEdge,
            .wrapT     = TextureWrap::ClampToEdge,
        },
        equirectData);

    // ── Cube + quad meshes + capture material common state ────────────────────
    const auto cubeHandle = renderer.createMesh(
        sonnet::primitives::makeBox(glm::vec3{2.0f}));  // [-1,+1]
    const auto quadHandle = renderer.createMesh(
        sonnet::primitives::makeQuad(glm::vec2{2.0f})); // [-1,+1] fullscreen

    // ── Step 1: env cubemap (equirect → cubemap) ──────────────────────────────
    const auto envShader = renderer.createShader(
        loadFile(shaderDir / "ibl/capture.vert"),
        loadFile(shaderDir / "ibl/equirect_to_cube.frag"));
    const auto envMatTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = envShader,
        .renderState  = {},
    });
    MaterialInstance envMat{envMatTmpl};
    envMat.addTexture("uEquirectMap", equirectHandle);

    const auto envCubeRT = renderer.createRenderTarget(RenderTargetDesc{
        .width     = ENV_SIZE,
        .height    = ENV_SIZE,
        .colors    = {{TextureFormat::RGBA16F,
                       SamplerDesc{.minFilter = MinFilter::LinearMipmapLinear,
                                   .magFilter = MagFilter::Linear,
                                   .wrapS     = TextureWrap::ClampToEdge,
                                   .wrapT     = TextureWrap::ClampToEdge,
                                   .wrapR     = TextureWrap::ClampToEdge}}},
        .depth     = RenderBufferDesc{},
        .isCubemap = true,
        .mipLevels = 1,
    });
    {
        std::vector<RenderItem> q{{
            .mesh        = cubeHandle,
            .material    = envMat,
            .modelMatrix = glm::mat4{1.0f},
        }};
        for (std::uint32_t face = 0; face < 6; ++face) {
            renderFace(renderer, backend, envCubeRT, face, 0, ENV_SIZE,
                        VIEWS[face], PROJ, q);
        }
    }
    const auto envCubeHandle = renderer.colorTextureHandle(envCubeRT, 0);

    // ── Step 2: irradiance cubemap ────────────────────────────────────────────
    const auto irradShader = renderer.createShader(
        loadFile(shaderDir / "ibl/capture.vert"),
        loadFile(shaderDir / "ibl/irradiance.frag"));
    const auto irradMatTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = irradShader,
        .renderState  = {},
    });
    MaterialInstance irradMat{irradMatTmpl};
    irradMat.addTexture("uEnvMap", envCubeHandle);

    const auto irradCubeRT = renderer.createRenderTarget(RenderTargetDesc{
        .width     = IRRAD_SIZE,
        .height    = IRRAD_SIZE,
        .colors    = {{TextureFormat::RGBA16F,
                       SamplerDesc{.minFilter = MinFilter::Linear,
                                   .magFilter = MagFilter::Linear,
                                   .wrapS     = TextureWrap::ClampToEdge,
                                   .wrapT     = TextureWrap::ClampToEdge,
                                   .wrapR     = TextureWrap::ClampToEdge}}},
        .depth     = RenderBufferDesc{},
        .isCubemap = true,
        .mipLevels = 1,
    });
    {
        std::vector<RenderItem> q{{
            .mesh        = cubeHandle,
            .material    = irradMat,
            .modelMatrix = glm::mat4{1.0f},
        }};
        for (std::uint32_t face = 0; face < 6; ++face) {
            renderFace(renderer, backend, irradCubeRT, face, 0, IRRAD_SIZE,
                        VIEWS[face], PROJ, q);
        }
    }
    const auto irradianceHandle = renderer.colorTextureHandle(irradCubeRT, 0);

    // ── Step 3: prefiltered specular cubemap (5 mip levels) ───────────────────
    const auto preShader = renderer.createShader(
        loadFile(shaderDir / "ibl/capture.vert"),
        loadFile(shaderDir / "ibl/prefilter.frag"));
    const auto preMatTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = preShader,
        .renderState  = {},
    });
    const auto prefilterRT = renderer.createRenderTarget(RenderTargetDesc{
        .width     = PREFILTER_SIZE,
        .height    = PREFILTER_SIZE,
        .colors    = {{TextureFormat::RGBA16F,
                       SamplerDesc{.minFilter = MinFilter::LinearMipmapLinear,
                                   .magFilter = MagFilter::Linear,
                                   .wrapS     = TextureWrap::ClampToEdge,
                                   .wrapT     = TextureWrap::ClampToEdge,
                                   .wrapR     = TextureWrap::ClampToEdge}}},
        .depth     = RenderBufferDesc{},
        .isCubemap = true,
        .mipLevels = NUM_MIPS,
    });
    for (int mip = 0; mip < NUM_MIPS; ++mip) {
        const std::uint32_t mipSize = std::max(1u, static_cast<std::uint32_t>(PREFILTER_SIZE) >> mip);
        const float roughness       = static_cast<float>(mip) / static_cast<float>(NUM_MIPS - 1);

        MaterialInstance preMat{preMatTmpl};
        preMat.addTexture("uEnvMap", envCubeHandle);
        preMat.set("uRoughness", roughness);

        std::vector<RenderItem> q{{
            .mesh        = cubeHandle,
            .material    = preMat,
            .modelMatrix = glm::mat4{1.0f},
        }};
        for (std::uint32_t face = 0; face < 6; ++face) {
            renderFace(renderer, backend, prefilterRT, face,
                        static_cast<std::uint32_t>(mip), mipSize,
                        VIEWS[face], PROJ, q);
        }
    }
    const auto prefilteredHandle = renderer.colorTextureHandle(prefilterRT, 0);

    // ── Step 4: BRDF LUT (single 2D pass) ─────────────────────────────────────
    const auto lutShader = renderer.createShader(
        loadFile(shaderDir / "ibl/brdf_lut.vert"),
        loadFile(shaderDir / "ibl/brdf_lut.frag"));
    const auto lutMatTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = lutShader,
        .renderState  = {},
    });
    MaterialInstance lutMat{lutMatTmpl};

    const auto lutRT = renderer.createRenderTarget(RenderTargetDesc{
        .width  = LUT_SIZE,
        .height = LUT_SIZE,
        .colors = {{TextureFormat::RG16F,
                    SamplerDesc{.minFilter = MinFilter::Linear,
                                .magFilter = MagFilter::Linear,
                                .wrapS     = TextureWrap::ClampToEdge,
                                .wrapT     = TextureWrap::ClampToEdge}}},
        .depth  = RenderBufferDesc{},
    });
    {
        std::vector<RenderItem> q{{
            .mesh        = quadHandle,
            .material    = lutMat,
            .modelMatrix = glm::mat4{1.0f},
        }};
        FrameContext ctx{
            .viewMatrix       = glm::mat4{1.0f},
            .projectionMatrix = glm::mat4{1.0f},
            .viewPosition     = glm::vec3{0.0f},
            .viewportWidth    = LUT_SIZE,
            .viewportHeight   = LUT_SIZE,
            .deltaTime        = 0.0f,
        };
        renderer.beginFrame();
        renderer.bindRenderTarget(lutRT);
        backend.setViewport(LUT_SIZE, LUT_SIZE);
        backend.setDepthTest(false);
        backend.setDepthWrite(false);
        backend.clear({
            .colors = {{0, glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}}},
            .depth  = 1.0f,
        });
        renderer.render(ctx, q);
        renderer.endFrame();
    }
    const auto brdfLUTHandle = renderer.colorTextureHandle(lutRT, 0);

    return IBLMaps{
        .equirectHandle    = equirectHandle,
        .irradianceHandle  = irradianceHandle,
        .prefilteredHandle = prefilteredHandle,
        .brdfLUTHandle     = brdfLUTHandle,
        .prefilteredLODs   = NUM_MIPS,
    };
}
