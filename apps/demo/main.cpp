// Sonnet v2 — Phase 25+26 demo
// Shadow map -> HDR offscreen pass (skybox + lit geometry) -> ACES tone-mapping; ImGui debug panel.

#include "IBL.h"

#include <sonnet/api/render/Light.h>
#include <sonnet/input/InputSystem.h>
#include <sonnet/loaders/ShaderLoader.h>
#include <sonnet/primitives/MeshPrimitives.h>
#include <sonnet/renderer/frontend/Renderer.h>
#include <sonnet/renderer/opengl/GlRendererBackend.h>
#include <sonnet/scene/SceneLoader.h>
#include <sonnet/ui/ImGuiLayer.h>
#include <sonnet/window/GLFWInputAdapter.h>
#include <sonnet/window/GLFWWindow.h>
#include <sonnet/world/Scene.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

// ── Fly camera ────────────────────────────────────────────────────────────────
// Input controller that drives a Transform. Projection parameters live in the
// CameraComponent on the same GameObject — this class owns neither.

class FlyCamera {
public:
    explicit FlyCamera(sonnet::world::Transform &transform) : m_transform(transform) {
        m_transform.setLocalPosition({0.0f, 0.0f, 3.0f});
        applyOrientation();
    }

    void update(float dt, const sonnet::input::InputSystem &input) {
        using sonnet::api::input::Key;

        const glm::vec2 delta = input.mouseDelta();
        m_yaw   += delta.x * m_sensitivity;
        m_pitch  = std::clamp(m_pitch - delta.y * m_sensitivity, -89.0f, 89.0f);
        applyOrientation();

        const glm::vec3 front = m_transform.forward();
        const glm::vec3 right = m_transform.right();
        const float     speed = m_speed * dt;

        glm::vec3 pos = m_transform.getLocalPosition();
        if (input.isKeyDown(Key::W)) pos += front    * speed;
        if (input.isKeyDown(Key::S)) pos -= front    * speed;
        if (input.isKeyDown(Key::D)) pos += right    * speed;
        if (input.isKeyDown(Key::A)) pos -= right    * speed;
        if (input.isKeyDown(Key::E)) pos += WORLD_UP * speed;
        if (input.isKeyDown(Key::Q)) pos -= WORLD_UP * speed;
        m_transform.setLocalPosition(pos);
    }

private:
    static constexpr glm::vec3 WORLD_UP{0.0f, 1.0f, 0.0f};

    void applyOrientation() {
        const float yr = glm::radians(m_yaw);
        const float pr = glm::radians(m_pitch);
        const glm::vec3 front = glm::normalize(glm::vec3{
            std::cos(yr) * std::cos(pr),
            std::sin(pr),
            std::sin(yr) * std::cos(pr),
        });
        m_transform.setLocalRotation(glm::quatLookAt(front, WORLD_UP));
    }

    sonnet::world::Transform &m_transform;
    float m_yaw{-90.0f}, m_pitch{0.0f};
    float m_speed{5.0f}, m_sensitivity{0.1f};
};

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    sonnet::window::GLFWWindow window{{1280, 720, "Sonnet v2 Demo"}};
    sonnet::input::InputSystem input;
    sonnet::window::GLFWInputAdapter adapter{input};
    window.setInputAdapter(&adapter);

    sonnet::renderer::opengl::GlRendererBackend backend;
    backend.initialize();

    sonnet::ui::ImGuiLayer imgui;
    imgui.init(window.handle());

    sonnet::renderer::frontend::Renderer renderer{backend};

    // ── Cascaded shadow map render targets (3 × Depth24, 2048²) ─────────────────
    constexpr std::uint32_t SHADOW_SIZE  = 2048;
    constexpr int           NUM_CASCADES = 3;

    const sonnet::api::render::SamplerDesc csmSamplerDesc{
        .minFilter    = sonnet::api::render::MinFilter::Linear,
        .magFilter    = sonnet::api::render::MagFilter::Linear,
        .wrapS        = sonnet::api::render::TextureWrap::ClampToEdge,
        .wrapT        = sonnet::api::render::TextureWrap::ClampToEdge,
        .depthCompare = true,
    };
    std::array<sonnet::core::RenderTargetHandle, NUM_CASCADES> csmRTHandles{};
    std::array<sonnet::core::GPUTextureHandle,       NUM_CASCADES> csmDepthHandles{};
    for (int i = 0; i < NUM_CASCADES; ++i) {
        csmRTHandles[i] = renderer.createRenderTarget(sonnet::api::render::RenderTargetDesc{
            .width  = SHADOW_SIZE,
            .height = SHADOW_SIZE,
            .colors = {},
            .depth  = sonnet::api::render::TextureAttachmentDesc{
                .format      = sonnet::api::render::TextureFormat::Depth24,
                .samplerDesc = csmSamplerDesc,
            },
        });
        csmDepthHandles[i] = renderer.depthTextureHandle(csmRTHandles[i]);
    }

    // ── Point-light shadow cubemaps (4 × R32F, 512²) + shared depth RBO ─────
    constexpr int   MAX_SHADOW_LIGHTS = 4;
    constexpr int   POINT_SHADOW_SIZE = 512;
    constexpr float POINT_SHADOW_FAR  = 25.0f;

    std::array<GLuint, MAX_SHADOW_LIGHTS> pointShadowCubeTex{};
    glGenTextures(MAX_SHADOW_LIGHTS, pointShadowCubeTex.data());
    for (int i = 0; i < MAX_SHADOW_LIGHTS; ++i) {
        glBindTexture(GL_TEXTURE_CUBE_MAP, pointShadowCubeTex[i]);
        for (int f = 0; f < 6; ++f)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0,
                         GL_R32F, POINT_SHADOW_SIZE, POINT_SHADOW_SIZE, 0,
                         GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    GLuint pointShadowFBO = 0, pointShadowDepthRBO = 0;
    glGenFramebuffers(1, &pointShadowFBO);
    glGenRenderbuffers(1, &pointShadowDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, pointShadowDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          POINT_SHADOW_SIZE, POINT_SHADOW_SIZE);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Attach depth RBO to the shadow FBO once; colour attachment is swapped per face.
    glBindFramebuffer(GL_FRAMEBUFFER, pointShadowFBO);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, pointShadowDepthRBO);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Register cubemaps with the engine renderer for use in materials.
    std::array<sonnet::core::GPUTextureHandle, MAX_SHADOW_LIGHTS> pointShadowHandles{};
    for (int i = 0; i < MAX_SHADOW_LIGHTS; ++i)
        pointShadowHandles[i] = renderer.registerRawTexture(
            std::make_unique<RawGLCubeMap>(pointShadowCubeTex[i]));

    // ── Shadow-pass shader and material ───────────────────────────────────────
    const auto shadowVertSrc  = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/shadow.vert");
    const auto shadowFragSrc  = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/shadow.frag");
    const auto shadowShader   = renderer.createShader(shadowVertSrc, shadowFragSrc);
    const auto shadowMatTmpl  = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = shadowShader,
        .renderState  = {},
    });
    sonnet::api::render::MaterialInstance shadowMat{shadowMatTmpl};

    // ── SSAO hemisphere kernel (64 samples, biased towards origin) ───────────
    std::vector<glm::vec3> ssaoKernel(64);
    {
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        for (int i = 0; i < 64; ++i) {
            glm::vec3 sample{dist(rng) * 2.0f - 1.0f,
                             dist(rng) * 2.0f - 1.0f,
                             dist(rng)};
            sample = glm::normalize(sample) * dist(rng);
            const float scale = static_cast<float>(i) / 64.0f;
            ssaoKernel[i] = sample * glm::mix(0.1f, 1.0f, scale * scale);
        }
    }

    // ── SSAO noise texture (4×4, random tangent-space rotations) ─────────────
    GLuint ssaoNoiseTex = 0;
    {
        std::mt19937 rng(123);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        std::vector<glm::vec3> noiseData(16);
        for (auto &n : noiseData)
            n = glm::vec3{dist(rng) * 2.0f - 1.0f, dist(rng) * 2.0f - 1.0f, 0.0f};
        glGenTextures(1, &ssaoNoiseTex);
        glBindTexture(GL_TEXTURE_2D, ssaoNoiseTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, noiseData.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    const auto ssaoNoiseHandle = renderer.registerRawTexture(
        std::make_unique<RawGLTexture2D>(ssaoNoiseTex));

    // ── IBL — bake irradiance, pre-filtered specular, BRDF LUT ───────────────
    const IBLMaps ibl = buildIBL(
        renderer,
        DEMO_ASSETS_DIR "/kloppenheim_06_1k.hdr",
        DEMO_ASSETS_DIR "/shaders");

    // ── Scene — loaded from JSON (shaders + materials + meshes + objects) ─────
    sonnet::world::Scene scene;
    sonnet::scene::SceneLoader sceneLoader;
    const auto loaded = sceneLoader.load(
        DEMO_ASSETS_DIR "/scene.json",
        DEMO_ASSETS_DIR,
        scene,
        renderer);

    auto &arm       = *loaded.objects.at("Arm");
    auto &cube      = *loaded.objects.at("Cube");
    auto &floor     = *loaded.objects.at("Floor");
    auto &cameraObj = *loaded.objects.at("Camera");
    auto &lamp      = *loaded.objects.at("Lamp");
    auto &cubeMat   = cube.render->material;
    auto &floorMat  = floor.render->material;
    auto &lampMat   = lamp.render->material;
    // Cache the lamp's sphere mesh handle + emissive material template so we can
    // render small indicator spheres for lights 1-7 without touching the scene graph.
    const auto sphereMeshHandle      = lamp.render->mesh;
    const auto emissiveMatTemplate   = lamp.render->material.templateHandle();

    // ── Scene serialization ───────────────────────────────────────────────────
    // Writes current local transforms of root-level objects back to scene.json.
    // glTF sub-nodes (name contains '/') are skipped.
    static constexpr const char *kSceneFile = DEMO_ASSETS_DIR "/scene.json";
    auto saveScene = [&]() {
        std::ifstream inFile{kSceneFile};
        if (!inFile) return;
        nlohmann::json doc = nlohmann::json::parse(inFile, nullptr, /*exceptions=*/false);
        inFile.close();
        if (doc.is_discarded() || !doc.contains("objects")) return;

        for (auto &objSpec : doc["objects"]) {
            const std::string name = objSpec.value("name", "");
            if (name.empty() || name.find('/') != std::string::npos) continue;
            auto it = loaded.objects.find(name);
            if (it == loaded.objects.end()) continue;
            const auto &tf = it->second->transform;

            const auto p = tf.getLocalPosition();
            objSpec["position"] = {p.x, p.y, p.z};

            const auto r = tf.getLocalRotation();
            objSpec["rotation"] = {r.x, r.y, r.z, r.w};

            const auto s = tf.getLocalScale();
            if (s == glm::vec3{1.0f})
                objSpec.erase("scale");
            else
                objSpec["scale"] = {s.x, s.y, s.z};
        }

        std::ofstream outFile{kSceneFile};
        outFile << doc.dump(4) << '\n';
    };

    // ── Asset name caches (for the Assets browser panel) ─────────────────────
    std::vector<std::string> assetMeshNames, assetMaterialNames,
                             assetTextureNames, assetShaderNames;
    {
        std::ifstream f{kSceneFile};
        if (f) {
            auto doc = nlohmann::json::parse(f, nullptr, false);
            if (!doc.is_discarded() && doc.contains("assets")) {
                const auto &assets = doc["assets"];
                auto collect = [](const nlohmann::json &section,
                                  std::vector<std::string> &out) {
                    if (section.is_object())
                        for (const auto &[k, v] : section.items())
                            out.push_back(k);
                };
                collect(assets.value("meshes",    nlohmann::json::object()), assetMeshNames);
                collect(assets.value("materials", nlohmann::json::object()), assetMaterialNames);
                collect(assets.value("textures",  nlohmann::json::object()), assetTextureNames);
                collect(assets.value("shaders",   nlohmann::json::object()), assetShaderNames);
            }
        }
    }

    // ── HDR render target ─────────────────────────────────────────────────────
    const auto fbSize0    = window.getFrameBufferSize();
    const auto hdrRTHandle = renderer.createRenderTarget(sonnet::api::render::RenderTargetDesc{
        .width  = fbSize0.x,
        .height = fbSize0.y,
        .colors = {{
            .format      = sonnet::api::render::TextureFormat::RGBA16F,
            .samplerDesc = {
                .minFilter = sonnet::api::render::MinFilter::Linear,
                .magFilter = sonnet::api::render::MagFilter::Linear,
                .wrapS     = sonnet::api::render::TextureWrap::ClampToEdge,
                .wrapT     = sonnet::api::render::TextureWrap::ClampToEdge,
            },
        }},
        .depth  = sonnet::api::render::RenderBufferDesc{},
    });
    const auto hdrTexHandle = renderer.colorTextureHandle(hdrRTHandle, 0);

    // ── G-buffer render target (3 × RGBA16F colour + Depth24 texture) ───────────
    // Attachment 0: albedo.rgb + roughness.a
    // Attachment 1: world-space normal.rgb + metallic.a
    // Attachment 2: emissive.rgb + ORM-ao.a
    // Depth sampled by SSAO, sky discard, and FXAA edge gate.
    const auto nearestClamp = sonnet::api::render::SamplerDesc{
        .minFilter = sonnet::api::render::MinFilter::Nearest,
        .magFilter = sonnet::api::render::MagFilter::Nearest,
        .wrapS     = sonnet::api::render::TextureWrap::ClampToEdge,
        .wrapT     = sonnet::api::render::TextureWrap::ClampToEdge,
    };
    const auto linearClamp = sonnet::api::render::SamplerDesc{
        .minFilter = sonnet::api::render::MinFilter::Linear,
        .magFilter = sonnet::api::render::MagFilter::Linear,
        .wrapS     = sonnet::api::render::TextureWrap::ClampToEdge,
        .wrapT     = sonnet::api::render::TextureWrap::ClampToEdge,
    };
    const auto gbufRTHandle = renderer.createRenderTarget(sonnet::api::render::RenderTargetDesc{
        .width  = fbSize0.x,
        .height = fbSize0.y,
        .colors = {
            { .format = sonnet::api::render::TextureFormat::RGBA16F, .samplerDesc = nearestClamp },
            { .format = sonnet::api::render::TextureFormat::RGBA16F, .samplerDesc = nearestClamp },
            { .format = sonnet::api::render::TextureFormat::RGBA16F, .samplerDesc = nearestClamp },
        },
        .depth  = sonnet::api::render::TextureAttachmentDesc{
            .format      = sonnet::api::render::TextureFormat::Depth24,
            .samplerDesc = linearClamp,
        },
    });
    const auto gbufAlbedoRoughTex    = renderer.colorTextureHandle(gbufRTHandle, 0);
    const auto gbufNormalMetallicTex = renderer.colorTextureHandle(gbufRTHandle, 1);
    const auto gbufEmissiveAOTex     = renderer.colorTextureHandle(gbufRTHandle, 2);
    const auto gbufDepthTex          = renderer.depthTextureHandle(gbufRTHandle);

    // ── SSAO render targets (R32F, single-channel AO) ─────────────────────────
    const auto makeR32FRT = [&]() {
        return renderer.createRenderTarget(sonnet::api::render::RenderTargetDesc{
            .width  = fbSize0.x,
            .height = fbSize0.y,
            .colors = {{
                .format      = sonnet::api::render::TextureFormat::R32F,
                .samplerDesc = {
                    .minFilter = sonnet::api::render::MinFilter::Linear,
                    .magFilter = sonnet::api::render::MagFilter::Linear,
                    .wrapS     = sonnet::api::render::TextureWrap::ClampToEdge,
                    .wrapT     = sonnet::api::render::TextureWrap::ClampToEdge,
                },
            }},
        });
    };
    const auto ssaoRTHandle     = makeR32FRT();
    const auto ssaoBlurRTHandle = makeR32FRT();
    const auto ssaoTex          = renderer.colorTextureHandle(ssaoRTHandle,     0);
    const auto ssaoBlurTex      = renderer.colorTextureHandle(ssaoBlurRTHandle, 0);

    const float maxLOD = static_cast<float>(ibl.prefilteredLODs - 1);

    // ── Tone-mapping fullscreen quad ──────────────────────────────────────────
    const auto quadMesh       = sonnet::primitives::makeQuad({2.0f, 2.0f});
    const auto quadMeshHandle = renderer.createMesh(quadMesh);
    const auto tonemapVertSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/tonemap.vert");
    const auto tonemapFragSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/tonemap.frag");
    const auto tonemapShader  = renderer.createShader(tonemapVertSrc, tonemapFragSrc);
    const auto tonemapMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = tonemapShader,
        .renderState  = {
            .depthTest  = false,
            .depthWrite = false,
            .cull       = sonnet::api::render::CullMode::None,
        },
    });
    // ── LDR render target (tonemap output; FXAA reads from this) ─────────────
    const auto ldrRT = renderer.createRenderTarget(sonnet::api::render::RenderTargetDesc{
        .width  = fbSize0.x,
        .height = fbSize0.y,
        .colors = {{
            .format      = sonnet::api::render::TextureFormat::RGBA8,
            .samplerDesc = {
                .minFilter = sonnet::api::render::MinFilter::Linear,
                .magFilter = sonnet::api::render::MagFilter::Linear,
                .wrapS     = sonnet::api::render::TextureWrap::ClampToEdge,
                .wrapT     = sonnet::api::render::TextureWrap::ClampToEdge,
            },
        }},
    });
    const auto ldrTex = renderer.colorTextureHandle(ldrRT, 0);

    // ── Viewport render target (final output shown in ImGui Viewport panel) ────
    const auto viewportRT = renderer.createRenderTarget(sonnet::api::render::RenderTargetDesc{
        .width  = fbSize0.x,
        .height = fbSize0.y,
        .colors = {{
            .format      = sonnet::api::render::TextureFormat::RGBA8,
            .samplerDesc = {
                .minFilter = sonnet::api::render::MinFilter::Linear,
                .magFilter = sonnet::api::render::MagFilter::Linear,
                .wrapS     = sonnet::api::render::TextureWrap::ClampToEdge,
                .wrapT     = sonnet::api::render::TextureWrap::ClampToEdge,
            },
        }},
    });
    const auto viewportTex   = renderer.colorTextureHandle(viewportRT, 0);
    const auto viewportTexId = static_cast<GLuint>(renderer.nativeTextureId(viewportTex));

    // ── Bloom render targets (same resolution as HDR) ─────────────────────────
    const auto makeBloomRT = [&]() {
        return renderer.createRenderTarget(sonnet::api::render::RenderTargetDesc{
            .width  = fbSize0.x,
            .height = fbSize0.y,
            .colors = {{
                .format      = sonnet::api::render::TextureFormat::RGBA16F,
                .samplerDesc = {
                    .minFilter = sonnet::api::render::MinFilter::Linear,
                    .magFilter = sonnet::api::render::MagFilter::Linear,
                    .wrapS     = sonnet::api::render::TextureWrap::ClampToEdge,
                    .wrapT     = sonnet::api::render::TextureWrap::ClampToEdge,
                },
            }},
        });
    };
    const auto bloomBrightRT  = makeBloomRT();
    const auto bloomBlurRT    = makeBloomRT();
    const auto bloomBrightTex = renderer.colorTextureHandle(bloomBrightRT, 0);
    const auto bloomBlurTex   = renderer.colorTextureHandle(bloomBlurRT,   0);

    // ── SSR render target (RGBA16F — reflection colour) ───────────────────────
    const auto ssrRT  = makeBloomRT();
    const auto ssrTex = renderer.colorTextureHandle(ssrRT, 0);

    // ── Outline mask render target (RGBA8, no depth — full silhouette) ────────
    const auto outlineMaskRT = renderer.createRenderTarget(sonnet::api::render::RenderTargetDesc{
        .width  = fbSize0.x,
        .height = fbSize0.y,
        .colors = {{
            .format      = sonnet::api::render::TextureFormat::RGBA8,
            .samplerDesc = nearestClamp,
        }},
    });
    const auto outlineMaskTex = renderer.colorTextureHandle(outlineMaskRT, 0);

    // Bright-pass material.
    const auto noDepthState = sonnet::api::render::RenderState{
        .depthTest  = false,
        .depthWrite = false,
        .cull       = sonnet::api::render::CullMode::None,
    };
    const auto bloomBrightFragSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/bloom_bright.frag");
    const auto bloomBrightShader  = renderer.createShader(tonemapVertSrc, bloomBrightFragSrc);
    const auto bloomBrightMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = bloomBrightShader,
        .renderState  = noDepthState,
    });
    sonnet::api::render::MaterialInstance bloomBrightMat{bloomBrightMatTmpl};
    bloomBrightMat.addTexture("uHdrColor", hdrTexHandle);

    // Blur material (shared; texture swapped each iteration).
    const auto bloomBlurFragSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/bloom_blur.frag");
    const auto bloomBlurShader  = renderer.createShader(tonemapVertSrc, bloomBlurFragSrc);
    const auto bloomBlurMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = bloomBlurShader,
        .renderState  = noDepthState,
    });
    // Two material instances — one reading from bright, one reading from blur.
    sonnet::api::render::MaterialInstance bloomBlurHMat{bloomBlurMatTmpl}; // horizontal: bright → blur
    sonnet::api::render::MaterialInstance bloomBlurVMat{bloomBlurMatTmpl}; // vertical:   blur  → bright
    bloomBlurHMat.addTexture("uBloomTexture", bloomBrightTex);
    bloomBlurVMat.addTexture("uBloomTexture", bloomBlurTex);
    bloomBlurHMat.set("uHorizontal", 1); // non-zero int == true in GLSL
    bloomBlurVMat.set("uHorizontal", 0);

    // ── SSR shader and material ───────────────────────────────────────────────
    const auto ssrFragSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/ssr.frag");
    const auto ssrShader  = renderer.createShader(tonemapVertSrc, ssrFragSrc);
    const auto ssrMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = ssrShader,
        .renderState  = noDepthState,
    });
    sonnet::api::render::MaterialInstance ssrMat{ssrMatTmpl};
    ssrMat.addTexture("uDepth",           gbufDepthTex);
    ssrMat.addTexture("uNormalMetallic",  gbufNormalMetallicTex);
    ssrMat.addTexture("uAlbedoRoughness", gbufAlbedoRoughTex);
    ssrMat.addTexture("uHDRColor",        hdrTexHandle);

    sonnet::api::render::MaterialInstance tonemapMat{tonemapMatTmpl};
    tonemapMat.addTexture("uHdrColor",      hdrTexHandle);
    tonemapMat.addTexture("uBloomTexture",  bloomBrightTex); // final blur result ends up here
    tonemapMat.addTexture("uSSRTex",        ssrTex);

    // ── FXAA shader and material ──────────────────────────────────────────────
    const auto fxaaVertSrc  = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/fxaa.vert");
    const auto fxaaFragSrc  = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/fxaa.frag");
    const auto fxaaShader   = renderer.createShader(fxaaVertSrc, fxaaFragSrc);
    const auto fxaaMatTmpl  = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = fxaaShader,
        .renderState  = noDepthState,
    });
    sonnet::api::render::MaterialInstance fxaaMat{fxaaMatTmpl};
    fxaaMat.addTexture("uScreen", ldrTex);
    fxaaMat.addTexture("uDepth",  gbufDepthTex); // G-buffer depth for geometric edge gating

    // ── SSAO shader and material ───────────────────────────────────────────────
    const auto ssaoVertSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/ssao.vert");
    const auto ssaoFragSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/ssao.frag");
    const auto ssaoShader  = renderer.createShader(ssaoVertSrc, ssaoFragSrc);
    const auto ssaoMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = ssaoShader,
        .renderState  = noDepthState,
    });
    sonnet::api::render::MaterialInstance ssaoMat{ssaoMatTmpl};
    ssaoMat.addTexture("uNormalMap", gbufNormalMetallicTex); // G-buffer world normals
    ssaoMat.addTexture("uDepthMap",  gbufDepthTex);          // G-buffer depth
    ssaoMat.addTexture("uNoiseMap",  ssaoNoiseHandle);
    for (int i = 0; i < 64; ++i)
        ssaoMat.set("uKernel[" + std::to_string(i) + "]", ssaoKernel[i]);

    // ── SSAO blur shader and material ─────────────────────────────────────────
    const auto ssaoBlurFragSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/ssao_blur.frag");
    const auto ssaoBlurShader  = renderer.createShader(ssaoVertSrc, ssaoBlurFragSrc);
    const auto ssaoBlurMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = ssaoBlurShader,
        .renderState  = noDepthState,
    });
    sonnet::api::render::MaterialInstance ssaoBlurMat{ssaoBlurMatTmpl};
    ssaoBlurMat.addTexture("uSSAOTexture", ssaoTex);

    // ── SSAO debug: show raw AO buffer as grayscale ───────────────────────────
    const auto ssaoShowFragSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/ssao_show.frag");
    const auto ssaoShowShader  = renderer.createShader(ssaoVertSrc, ssaoShowFragSrc);
    const auto ssaoShowMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = ssaoShowShader,
        .renderState  = noDepthState,
    });
    sonnet::api::render::MaterialInstance ssaoShowMat{ssaoShowMatTmpl};
    ssaoShowMat.addTexture("uSSAO", ssaoBlurTex);

    // ── Selection outline: mask + composite materials ─────────────────────────
    const auto outlineMaskVertSrc = sonnet::loaders::ShaderLoader::load(
        DEMO_ASSETS_DIR "/shaders/shadow.vert");         // reuse — Position + MVP uniforms
    const auto outlineMaskFragSrc = sonnet::loaders::ShaderLoader::load(
        DEMO_ASSETS_DIR "/shaders/outline_mask.frag");
    const auto outlineMaskShader  = renderer.createShader(outlineMaskVertSrc, outlineMaskFragSrc);
    const auto outlineMaskMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = outlineMaskShader,
        .renderState  = {
            .depthTest  = false,  // show full silhouette regardless of occlusion
            .depthWrite = false,
            .cull       = sonnet::api::render::CullMode::None,
        },
    });
    sonnet::api::render::MaterialInstance outlineMaskMat{outlineMaskMatTmpl};

    // Skinned variant — uses bone matrices so animated meshes follow their pose.
    const auto outlineMaskSkinnedVertSrc = sonnet::loaders::ShaderLoader::load(
        DEMO_ASSETS_DIR "/shaders/outline_mask_skinned.vert");
    const auto outlineMaskSkinnedShader  = renderer.createShader(outlineMaskSkinnedVertSrc, outlineMaskFragSrc);
    const auto outlineMaskSkinnedMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = outlineMaskSkinnedShader,
        .renderState  = {
            .depthTest  = false,
            .depthWrite = false,
            .cull       = sonnet::api::render::CullMode::None,
        },
    });

    const auto outlineFragSrc  = sonnet::loaders::ShaderLoader::load(
        DEMO_ASSETS_DIR "/shaders/outline.frag");
    const auto outlineShader   = renderer.createShader(tonemapVertSrc, outlineFragSrc);
    const auto outlineMatTmpl  = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = outlineShader,
        .renderState  = {
            .depthTest  = false,
            .depthWrite = false,
            .blend      = sonnet::api::render::BlendMode::Alpha,
            .cull       = sonnet::api::render::CullMode::None,
        },
    });
    sonnet::api::render::MaterialInstance outlineMat{outlineMatTmpl};
    outlineMat.addTexture("uMask", outlineMaskTex);

    // ── Skybox ────────────────────────────────────────────────────────────────
    // Depth testing is done in the shader: sky.frag reads gDepth and discards
    // pixels where geometry was drawn (depth < 1.0), so no FBO depth is needed.
    const auto skyVertSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/sky.vert");
    const auto skyFragSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/sky.frag");
    const auto skyShader  = renderer.createShader(skyVertSrc, skyFragSrc);
    const auto skyMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = skyShader,
        .renderState  = {
            .depthTest  = false,
            .depthWrite = false,
            .cull       = sonnet::api::render::CullMode::None,
        },
    });
    sonnet::api::render::MaterialInstance skyMat{skyMatTmpl};
    skyMat.addTexture("uEnvMap", ibl.equirectHandle);
    skyMat.addTexture("gDepth",  gbufDepthTex);

    // ── Deferred lighting shader and material ─────────────────────────────────
    const auto deferredFragSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/deferred_lighting.frag");
    const auto deferredShader  = renderer.createShader(tonemapVertSrc, deferredFragSrc);
    const auto deferredMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = deferredShader,
        .renderState  = noDepthState,
    });
    sonnet::api::render::MaterialInstance deferredMat{deferredMatTmpl};
    deferredMat.addTexture("gAlbedoRoughness", gbufAlbedoRoughTex);
    deferredMat.addTexture("gNormalMetallic",  gbufNormalMetallicTex);
    deferredMat.addTexture("gEmissiveAO",      gbufEmissiveAOTex);
    deferredMat.addTexture("gDepth",           gbufDepthTex);
    for (int c = 0; c < NUM_CASCADES; ++c)
        deferredMat.addTexture("uShadowMaps[" + std::to_string(c) + "]", csmDepthHandles[c]);
    deferredMat.addTexture("uIrradianceMap",   ibl.irradianceHandle);
    deferredMat.addTexture("uPrefilteredMap",  ibl.prefilteredHandle);
    deferredMat.addTexture("uBRDFLUT",         ibl.brdfLUTHandle);
    deferredMat.addTexture("uSSAO",            ssaoBlurTex);
    for (int i = 0; i < MAX_SHADOW_LIGHTS; ++i)
        deferredMat.addTexture("uPointShadowMaps[" + std::to_string(i) + "]",
                               pointShadowHandles[i]);
    deferredMat.set("uMaxPrefilteredLOD",   maxLOD);
    deferredMat.set("uPointShadowFarPlane", POINT_SHADOW_FAR);

    // ── Point-shadow depth shader and material ────────────────────────────────
    const auto ptShadowVertSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/point_shadow.vert");
    const auto ptShadowFragSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/point_shadow.frag");
    const auto ptShadowShader  = renderer.createShader(ptShadowVertSrc, ptShadowFragSrc);
    const auto ptShadowMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = ptShadowShader,
        .renderState  = {},
    });
    sonnet::api::render::MaterialInstance ptShadowMat{ptShadowMatTmpl};

    FlyCamera flyCamera{cameraObj.transform};

    // Tweakable state exposed via ImGui.
    float     rotationSpeed  = 45.0f;
    // Directional light — seeded from scene.json; falls back to defaults.
    glm::vec3 lightDir       = loaded.directionalLights.empty()
                                   ? glm::vec3{0.6f, 1.0f, 0.4f}
                                   : loaded.directionalLights[0].direction;
    glm::vec3 lightColor     = loaded.directionalLights.empty()
                                   ? glm::vec3{1.0f, 1.0f, 1.0f}
                                   : loaded.directionalLights[0].color;
    float     lightIntensity = loaded.directionalLights.empty()
                                   ? 1.0f
                                   : loaded.directionalLights[0].intensity;
    float     exposure        = 1.0f;
    float     shadowBias      = 0.005f;
    float     bloomThreshold  = 0.8f;
    float     bloomIntensity  = 0.5f;
    int       bloomIterations = 3;
    bool      ssaoEnabled     = true;
    float     ssaoRadius      = 1.5f;
    float     ssaoBias        = 0.05f;
    bool      ssaoShow        = false; // debug: show raw AO buffer
    bool      fxaaEnabled     = true;
    bool      outlineEnabled   = true;
    glm::vec3 outlineColor{1.0f, 0.6f, 0.05f}; // warm orange
    bool      ssrEnabled      = true;
    int       ssrMaxSteps     = 64;
    float     ssrStepSize     = 0.1f;
    float     ssrThickness    = 0.2f;
    float     ssrMaxDistance  = 10.0f;
    float     ssrRoughnessMax = 0.4f;
    float     ssrStrength     = 1.0f;
    float     pointShadowBias = 0.008f;
    bool      viewportFocused = false; // updated each frame from ImGui, used next frame

    // Per-object PBR scalar multipliers — applied on top of the ORM texture.
    // 1.0 = let the texture drive everything.
    float cubeMetallic      = 1.0f;
    float cubeRoughness     = 1.0f;
    float floorMetallic     = 1.0f;
    float floorRoughness    = 1.0f;

    // ── Editable point lights ─────────────────────────────────────────────────
    // Light 0 is the lamp sphere — position tracks lamp.transform, color/strength
    // also drive the emissive material.  Remaining lights are freely placed.
    struct PointLightEdit {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float     intensity{3.0f};
        glm::vec3 position{0.0f}; // ignored for light 0 (uses lamp transform)
        bool      enabled{true};
    };
    std::vector<PointLightEdit> pointLights;
    // Seed from scene.json loaded lights.
    for (const auto &pl : loaded.pointLights) {
        pointLights.push_back({
            .color     = pl.color,
            .intensity = pl.intensity,
            .position  = pl.position,
        });
    }

    float  rotation = 0.0f;
    double prevTime = glfwGetTime();

    // Scene hierarchy selection state.
    sonnet::world::GameObject *selectedObject = nullptr;
    glm::vec3                  editEuler{0.0f}; // Euler angles (degrees) for selected object

    // ── Transform gizmo state ─────────────────────────────────────────────────
    enum class GizmoMode { Translate, Rotate, Scale };
    GizmoMode gizmoMode       = GizmoMode::Translate;
    int       gizmoHoverAxis  = 0;  // 0=none, 1=X, 2=Y, 3=Z
    int       gizmoActiveAxis = 0;
    glm::vec3 dragStartPos{};
    glm::quat dragStartRot{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 dragStartScale{1.0f};
    ImVec2    dragStartMouse{};
    float     dragAccum = 0.0f;

    while (!window.shouldClose()) {
        const double now = glfwGetTime();
        const float  dt  = static_cast<float>(now - prevTime);
        prevTime = now;

        window.pollEvents();

        if (input.isKeyJustPressed(sonnet::api::input::Key::Escape))
            window.requestClose();

        const auto fbSize = window.getFrameBufferSize();

        // Camera controlled by RMB only when the 3D viewport panel is focused.
        if (viewportFocused && input.isMouseDown(sonnet::api::input::MouseButton::Right)) {
            window.captureCursor();
            flyCamera.update(dt, input);
        } else {
            window.releaseCursor();
        }

        rotation += rotationSpeed * dt;
        // Arm orbits around Y — the cube follows as a child.
        arm.transform.setLocalRotation(
            glm::angleAxis(glm::radians(rotation), glm::vec3{0, 1, 0}));
        // Cube also self-spins on its local X axis.
        cube.transform.setLocalRotation(
            glm::angleAxis(glm::radians(rotation * 0.5f), glm::vec3{1, 0, 0}));

        // ── Animation players ─────────────────────────────────────────────────
        for (const auto &obj : scene.objects()) {
            if (obj->animationPlayer)
                obj->animationPlayer->update(dt);
        }

        // ── Skinning bone palette upload ───────────────────────────────────────
        // Must run after animation players so bone transforms are current.
        for (const auto &obj : scene.objects()) {
            if (!obj->skin || !obj->render) continue;
            const auto &skin = *obj->skin;
            for (int bi = 0; bi < skin.numBones; ++bi) {
                if (!skin.boneTransforms[bi]) continue;
                const glm::mat4 boneMatrix =
                    skin.boneTransforms[bi]->getModelMatrix() * skin.inverseBindMatrices[bi];
                obj->render->material.set(
                    "uBoneMatrices[" + std::to_string(bi) + "]", boneMatrix);
            }
        }

        // ── Camera matrices (needed for CSM frustum extraction) ──────────────
        const float aspect = fbSize.x > 0 && fbSize.y > 0
            ? static_cast<float>(fbSize.x) / static_cast<float>(fbSize.y)
            : 16.0f / 9.0f;
        const glm::mat4 viewMat    = cameraObj.camera->viewMatrix(cameraObj.transform);
        const glm::mat4 projMat    = cameraObj.camera->projectionMatrix(aspect);
        const glm::mat4 invProjMat = glm::inverse(projMat);
        const glm::vec3 camPos     = cameraObj.transform.getWorldPosition();

        // ── Pass 1: Cascaded shadow maps ──────────────────────────────────────
        // Split view frustum [near, shadowFar] into NUM_CASCADES slices using
        // a blend (lambda=0.75) of logarithmic and uniform distributions.
        const float csmNear      = cameraObj.camera->near;
        const float csmFar       = 50.0f; // shadow range (shorter than camera far)
        constexpr float csmLambda = 0.75f;

        // Cascade split depths in view space (positive, camera-space Z).
        std::array<float, NUM_CASCADES> csmSplitDepths{};
        for (int i = 0; i < NUM_CASCADES; ++i) {
            const float p       = static_cast<float>(i + 1) / static_cast<float>(NUM_CASCADES);
            const float logSplit = csmNear * std::pow(csmFar / csmNear, p);
            const float uniSplit = csmNear + (csmFar - csmNear) * p;
            csmSplitDepths[i] = csmLambda * logSplit + (1.0f - csmLambda) * uniSplit;
        }

        // Light-view matrix (shared across all cascades).
        const glm::vec3 lightDirNorm = glm::normalize(lightDir);
        const glm::vec3 lightUp = std::abs(lightDirNorm.y) > 0.99f
                                ? glm::vec3{0.0f, 0.0f, 1.0f}
                                : glm::vec3{0.0f, 1.0f, 0.0f};
        const glm::mat4 lightView = glm::lookAt(-lightDirNorm, glm::vec3{0.0f}, lightUp);

        // NDC corners of a unit cube (clip-space frustum corners).
        const glm::vec4 ndcCorners[8] = {
            {-1,-1,-1, 1}, { 1,-1,-1, 1}, {-1, 1,-1, 1}, { 1, 1,-1, 1},
            {-1,-1, 1, 1}, { 1,-1, 1, 1}, {-1, 1, 1, 1}, { 1, 1, 1, 1},
        };

        // Build shadow geometry queue (same for all cascades).
        std::vector<sonnet::api::render::RenderItem> shadowQueue;
        for (const auto &obj : scene.objects()) {
            if (!obj->render) continue;
            shadowQueue.push_back({
                .mesh        = obj->render->mesh,
                .material    = shadowMat,
                .modelMatrix = obj->transform.getModelMatrix(),
            });
        }

        std::array<glm::mat4, NUM_CASCADES> csmLightSpaceMats{};
        float prevSplitDepth = csmNear;
        for (int c = 0; c < NUM_CASCADES; ++c) {
            const float splitDepth = csmSplitDepths[c];

            // Build a projection for this cascade slice [prevSplitDepth, splitDepth].
            const glm::mat4 cascadeProj = glm::perspective(
                glm::radians(cameraObj.camera->fov), aspect,
                prevSplitDepth, splitDepth);
            const glm::mat4 invCamVP = glm::inverse(cascadeProj * viewMat);

            // Transform NDC corners to world space, then to light space.
            float minX =  1e9f, maxX = -1e9f;
            float minY =  1e9f, maxY = -1e9f;
            for (const auto &nc : ndcCorners) {
                glm::vec4 world = invCamVP * nc;
                world /= world.w;
                glm::vec4 ls = lightView * world;
                minX = std::min(minX, ls.x); maxX = std::max(maxX, ls.x);
                minY = std::min(minY, ls.y); maxY = std::max(maxY, ls.y);
            }
            // Fixed generous z range to capture shadow casters outside the frustum.
            const glm::mat4 cascadeOrtho =
                glm::ortho(minX, maxX, minY, maxY, -100.0f, 100.0f);
            csmLightSpaceMats[c] = cascadeOrtho * lightView;

            renderer.bindRenderTarget(csmRTHandles[c]);
            backend.setViewport(SHADOW_SIZE, SHADOW_SIZE);
            glDepthMask(GL_TRUE);
            backend.clear({ .depth = 1.0f });

            sonnet::api::render::FrameContext shadowCtx{
                .viewMatrix       = lightView,
                .projectionMatrix = cascadeOrtho,
                .viewPosition     = glm::vec3{0.0f},
                .viewportWidth    = SHADOW_SIZE,
                .viewportHeight   = SHADOW_SIZE,
                .deltaTime        = 0.0f,
            };
            renderer.beginFrame();
            renderer.render(shadowCtx, shadowQueue);
            renderer.endFrame();

            prevSplitDepth = splitDepth;
        }

        // Upload cascade matrices and split depths to the deferred lighting shader.
        for (int c = 0; c < NUM_CASCADES; ++c) {
            deferredMat.set("uCSMLightSpaceMats[" + std::to_string(c) + "]", csmLightSpaceMats[c]);
            deferredMat.set("uCSMSplitDepths["   + std::to_string(c) + "]", csmSplitDepths[c]);
        }

        // Post-process helpers (identity view/proj + fullscreen quad renderer).
        const glm::mat4 identity{1.0f};
        const glm::vec3 origin{0.0f};
        sonnet::api::render::FrameContext ppCtx{
            .viewMatrix       = identity,
            .projectionMatrix = identity,
            .viewPosition     = origin,
            .viewportWidth    = fbSize.x,
            .viewportHeight   = fbSize.y,
            .deltaTime        = 0.0f,
        };
        const auto fullscreenQuad = [&](sonnet::api::render::MaterialInstance &mat) {
            std::vector<sonnet::api::render::RenderItem> q{{
                .mesh        = quadMeshHandle,
                .material    = mat,
                .modelMatrix = identity,
            }};
            renderer.beginFrame();
            renderer.render(ppCtx, q);
            renderer.endFrame();
        };

        // ── Per-frame material updates ─────────────────────────────────────────
        cubeMat.set("uMetallic",    cubeMetallic);
        cubeMat.set("uRoughness",   cubeRoughness);
        floorMat.set("uMetallic",   floorMetallic);
        floorMat.set("uRoughness",  floorRoughness);
        // Light 0 drives the lamp sphere's emissive visual.
        if (!pointLights.empty()) {
            lampMat.set("uEmissiveColor",    pointLights[0].color);
            lampMat.set("uEmissiveStrength", pointLights[0].intensity);
        }

        // Build the PointLight array for the deferred lighting pass.
        const glm::vec3 lampPos = lamp.transform.getWorldPosition();
        std::vector<sonnet::api::render::PointLight> ctxPointLights;
        for (int i = 0; i < static_cast<int>(pointLights.size()); ++i) {
            if (!pointLights[i].enabled) continue;
            const glm::vec3 pos = (i == 0) ? lampPos : pointLights[i].position;
            ctxPointLights.push_back({
                .position  = pos,
                .color     = pointLights[i].color,
                .intensity = pointLights[i].intensity,
            });
            if (ctxPointLights.size() >= 8) break; // clamp to shader MAX_POINT_LIGHTS
        }

        sonnet::api::render::FrameContext ctx{
            .viewMatrix       = viewMat,
            .projectionMatrix = projMat,
            .viewPosition     = camPos,
            .viewportWidth    = fbSize.x,
            .viewportHeight   = fbSize.y,
            .deltaTime        = dt,
            .directionalLight = sonnet::api::render::DirectionalLight{
                .direction = lightDir,
                .color     = lightColor,
                .intensity = lightIntensity,
            },
            .pointLights      = ctxPointLights,
        };

        // ── Pass 1.2: point-light shadow cubemaps ─────────────────────────────
        // Render scene geometry into each enabled light's depth cubemap (6 faces).
        // Up to MAX_SHADOW_LIGHTS lights cast shadows; additional lights illuminate only.
        int shadowLightCount = 0;
        {
            // 6 face views relative to each light position (90° FOV cube capture).
            const glm::mat4 ptShadowProj = glm::perspective(
                glm::radians(90.0f), 1.0f, 0.01f, POINT_SHADOW_FAR);
            auto faceViewsFor = [](const glm::vec3 &p) {
                return std::array<glm::mat4, 6>{
                    glm::lookAt(p, p + glm::vec3{ 1, 0, 0}, {0,-1, 0}),
                    glm::lookAt(p, p + glm::vec3{-1, 0, 0}, {0,-1, 0}),
                    glm::lookAt(p, p + glm::vec3{ 0, 1, 0}, {0, 0, 1}),
                    glm::lookAt(p, p + glm::vec3{ 0,-1, 0}, {0, 0,-1}),
                    glm::lookAt(p, p + glm::vec3{ 0, 0, 1}, {0,-1, 0}),
                    glm::lookAt(p, p + glm::vec3{ 0, 0,-1}, {0,-1, 0}),
                };
            };

            // Build shadow geometry queue (scene objects only — not indicator spheres).
            std::vector<sonnet::api::render::RenderItem> ptShadowQueue;
            for (const auto &obj : scene.objects()) {
                if (!obj->render) continue;
                ptShadowQueue.push_back({
                    .mesh        = obj->render->mesh,
                    .material    = ptShadowMat,
                    .modelMatrix = obj->transform.getModelMatrix(),
                });
            }

            glBindFramebuffer(GL_FRAMEBUFFER, pointShadowFBO);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
            backend.setViewport(POINT_SHADOW_SIZE, POINT_SHADOW_SIZE);

            for (const auto &pl : ctxPointLights) {
                if (shadowLightCount >= MAX_SHADOW_LIGHTS) break;
                const int   shadowIdx = shadowLightCount;
                const auto  faceViews = faceViewsFor(pl.position);
                ptShadowMat.set("uLightPos",  pl.position);
                ptShadowMat.set("uFarPlane",  POINT_SHADOW_FAR);

                for (int f = 0; f < 6; ++f) {
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                           GL_TEXTURE_CUBE_MAP_POSITIVE_X + f,
                                           pointShadowCubeTex[shadowIdx], 0);
                    glDrawBuffer(GL_COLOR_ATTACHMENT0);
                    glClearColor(1.0f, 0.0f, 0.0f, 1.0f); // far = max distance
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                    const sonnet::api::render::FrameContext faceCtx{
                        .viewMatrix       = faceViews[f],
                        .projectionMatrix = ptShadowProj,
                        .viewPosition     = pl.position,
                        .viewportWidth    = POINT_SHADOW_SIZE,
                        .viewportHeight   = POINT_SHADOW_SIZE,
                        .deltaTime        = 0.0f,
                    };
                    renderer.beginFrame();
                    renderer.render(faceCtx, ptShadowQueue);
                    renderer.endFrame();
                }
                ++shadowLightCount;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // restore default clear colour
        }
        deferredMat.set("uPointShadowCount", shadowLightCount);
        deferredMat.set("uPointShadowBias",  pointShadowBias);

        // ── Pass 1.5: G-buffer — all scene geometry → gbufRT ──────────────────
        renderer.bindRenderTarget(gbufRTHandle);
        backend.setViewport(fbSize.x, fbSize.y);
        glDepthMask(GL_TRUE);
        backend.clear({
            .colors = {
                {0, {0.0f, 0.0f, 0.0f, 1.0f}},
                {1, {0.0f, 0.0f, 0.0f, 1.0f}},
                {2, {0.0f, 0.0f, 0.0f, 1.0f}},
            },
            .depth = 1.0f,
        });
        {
            std::vector<sonnet::api::render::RenderItem> gbufQueue;
            scene.buildRenderQueue(gbufQueue);

            // Add small emissive indicator spheres for lights 1+ (light 0 is the lamp sphere).
            for (int i = 1; i < static_cast<int>(pointLights.size()); ++i) {
                if (!pointLights[i].enabled) continue;
                sonnet::api::render::MaterialInstance indMat{emissiveMatTemplate};
                indMat.set("uEmissiveColor",    pointLights[i].color);
                indMat.set("uEmissiveStrength", pointLights[i].intensity);
                const glm::mat4 model =
                    glm::translate(glm::mat4{1.0f}, pointLights[i].position) *
                    glm::scale(glm::mat4{1.0f}, glm::vec3{0.08f});
                gbufQueue.push_back({
                    .mesh        = sphereMeshHandle,
                    .material    = indMat,
                    .modelMatrix = model,
                });
            }

            renderer.beginFrame();
            renderer.render(ctx, gbufQueue);
            renderer.endFrame();
        }

        // ── Pass 1.6: SSAO → ssaoRT (or clear to white if disabled) ──────────
        if (ssaoEnabled) {
            renderer.bindRenderTarget(ssaoRTHandle);
            backend.setViewport(fbSize.x, fbSize.y);
            backend.clear({ .colors = {{0, {1.0f, 1.0f, 1.0f, 1.0f}}} });
            ssaoMat.set("uView",          viewMat);
            ssaoMat.set("uProjection",    projMat);
            ssaoMat.set("uInvProjection", invProjMat);
            ssaoMat.set("uNoiseScale",    glm::vec2{
                static_cast<float>(fbSize.x) / 4.0f,
                static_cast<float>(fbSize.y) / 4.0f});
            ssaoMat.set("uRadius", ssaoRadius);
            ssaoMat.set("uBias",   ssaoBias);
            fullscreenQuad(ssaoMat);

            // Pass 1.7: SSAO blur → ssaoBlurRT
            renderer.bindRenderTarget(ssaoBlurRTHandle);
            backend.setViewport(fbSize.x, fbSize.y);
            backend.clear({ .colors = {{0, {1.0f, 1.0f, 1.0f, 1.0f}}} });
            fullscreenQuad(ssaoBlurMat);
        } else {
            // Fill ssaoBlurRT with 1.0 (no occlusion).
            renderer.bindRenderTarget(ssaoBlurRTHandle);
            backend.setViewport(fbSize.x, fbSize.y);
            backend.clear({ .colors = {{0, {1.0f, 1.0f, 1.0f, 1.0f}}} });
        }

        // ── Pass 2: Deferred lighting — G-buffer + SSAO + IBL + shadow → hdrRT
        renderer.bindRenderTarget(hdrRTHandle);
        backend.setViewport(fbSize.x, fbSize.y);
        backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
        {
            const glm::mat4 invViewProjMat = glm::inverse(projMat * viewMat);
            deferredMat.set("uInvViewProj", invViewProjMat);
            deferredMat.set("uShadowBias",  shadowBias);
            std::vector<sonnet::api::render::RenderItem> dq{{
                .mesh        = quadMeshHandle,
                .material    = deferredMat,
                .modelMatrix = identity,
            }};
            renderer.beginFrame();
            renderer.render(ctx, dq);  // real ctx — auto-uploads lights, view pos, shadow matrix
            renderer.endFrame();
        }

        // ── Pass 2.1: Sky → hdrRT (discards pixels where G-buffer depth < 1.0)
        {
            std::vector<sonnet::api::render::RenderItem> skyQ{{
                .mesh        = quadMeshHandle,
                .material    = skyMat,
                .modelMatrix = identity,
            }};
            renderer.beginFrame();
            renderer.render(ctx, skyQ);
            renderer.endFrame();
        }

        // ── Pass 2.15: selection outline ──────────────────────────────────────
        if (outlineEnabled && selectedObject) {
            // Collect the selected object and all its scene-graph descendants.
            std::vector<sonnet::api::render::RenderItem> outlineQueue;
            std::function<void(sonnet::world::GameObject &)> collectSubtree =
                [&](sonnet::world::GameObject &obj) {
                    if (obj.render) {
                        if (obj.skin) {
                            // Skinned mesh: use skinned mask shader and upload
                            // the current bone palette so the outline follows
                            // the animated pose.
                            sonnet::api::render::MaterialInstance skinnedMaskMat{outlineMaskSkinnedMatTmpl};
                            for (const auto &[name, val] : obj.render->material.values()) {
                                if (name.rfind("uBoneMatrices", 0) == 0)
                                    skinnedMaskMat.set(name, val);
                            }
                            outlineQueue.push_back({
                                .mesh        = obj.render->mesh,
                                .material    = skinnedMaskMat,
                                .modelMatrix = obj.transform.getModelMatrix(),
                            });
                        } else {
                            outlineQueue.push_back({
                                .mesh        = obj.render->mesh,
                                .material    = outlineMaskMat,
                                .modelMatrix = obj.transform.getModelMatrix(),
                            });
                        }
                    }
                    for (auto *childTf : obj.transform.children()) {
                        for (auto &o : scene.objects())
                            if (&o->transform == childTf) { collectSubtree(*o); break; }
                    }
                };
            collectSubtree(*selectedObject);

            // Render subtree as solid white → outlineMaskRT.
            renderer.bindRenderTarget(outlineMaskRT);
            backend.setViewport(fbSize.x, fbSize.y);
            backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
            renderer.beginFrame();
            renderer.render(ctx, outlineQueue);
            renderer.endFrame();

            // Edge-detect the mask and alpha-blend the outline color onto hdrRT.
            renderer.bindRenderTarget(hdrRTHandle);
            backend.setViewport(fbSize.x, fbSize.y);
            outlineMat.set("uOutlineColor", outlineColor);
            fullscreenQuad(outlineMat);
        }

        // ── Pass 2.2: SSR → ssrRT ─────────────────────────────────────────────
        renderer.bindRenderTarget(ssrRT);
        backend.setViewport(fbSize.x, fbSize.y);
        backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
        if (ssrEnabled) {
            ssrMat.set("uProjection",    projMat);
            ssrMat.set("uInvProjection", invProjMat);
            ssrMat.set("uView",          viewMat);
            ssrMat.set("uResolution",    glm::vec2(static_cast<float>(fbSize.x),
                                                   static_cast<float>(fbSize.y)));
            ssrMat.set("uMaxSteps",      ssrMaxSteps);
            ssrMat.set("uStepSize",      ssrStepSize);
            ssrMat.set("uThickness",     ssrThickness);
            ssrMat.set("uMaxDistance",   ssrMaxDistance);
            ssrMat.set("uRoughnessMax",  ssrRoughnessMax);
            fullscreenQuad(ssrMat);
        }

        // ── Pass 2.5: bloom ────────────────────────────────────────────────────

        // Bright-pass extract.
        renderer.bindRenderTarget(bloomBrightRT);
        backend.setViewport(fbSize.x, fbSize.y);
        backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
        bloomBrightMat.set("uBloomThreshold", bloomThreshold);
        fullscreenQuad(bloomBrightMat);

        // Ping-pong Gaussian blur.
        for (int i = 0; i < bloomIterations; ++i) {
            renderer.bindRenderTarget(bloomBlurRT);
            backend.setViewport(fbSize.x, fbSize.y);
            backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
            fullscreenQuad(bloomBlurHMat); // bright → blur (horizontal)

            renderer.bindRenderTarget(bloomBrightRT);
            backend.setViewport(fbSize.x, fbSize.y);
            backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
            fullscreenQuad(bloomBlurVMat); // blur → bright (vertical)
        }
        // bloomBrightRT now holds the final blurred bloom.

        tonemapMat.set("uExposure",       exposure);
        tonemapMat.set("uBloomIntensity", bloomIntensity);
        tonemapMat.set("uSSRStrength",    ssrEnabled ? ssrStrength : 0.0f);

        if (ssaoShow) {
            // ── Debug: show raw SSAO buffer as grayscale → viewportRT ────────
            renderer.bindRenderTarget(viewportRT);
            backend.setViewport(fbSize.x, fbSize.y);
            backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
            fullscreenQuad(ssaoShowMat);
        } else if (fxaaEnabled) {
            // ── Pass 3: tone-map HDR → ldrRT ─────────────────────────────────
            renderer.bindRenderTarget(ldrRT);
            backend.setViewport(fbSize.x, fbSize.y);
            backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
            fullscreenQuad(tonemapMat);

            // ── Pass 4: FXAA ldrRT → viewportRT ──────────────────────────────
            renderer.bindRenderTarget(viewportRT);
            backend.setViewport(fbSize.x, fbSize.y);
            backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
            fxaaMat.set("uTexelSize", glm::vec2(1.0f / fbSize.x, 1.0f / fbSize.y));
            fullscreenQuad(fxaaMat);
        } else {
            // ── Pass 3 (no FXAA): tone-map HDR → viewportRT ──────────────────
            renderer.bindRenderTarget(viewportRT);
            backend.setViewport(fbSize.x, fbSize.y);
            backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
            fullscreenQuad(tonemapMat);
        }

        // Bind default framebuffer for ImGui rendering.
        backend.bindDefaultRenderTarget();
        backend.setViewport(fbSize.x, fbSize.y);
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // ── ImGui: engine UI ───────────────────────────────────────────────────
        imgui.begin();

        // ── Main menu bar ──────────────────────────────────────────────────────
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) saveScene();
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) window.requestClose();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                ImGui::TextDisabled("No actions yet");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window")) {
                ImGui::TextDisabled("Drag panels to re-dock them");
                ImGui::EndMenu();
            }
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 80.0f);
            ImGui::TextDisabled("%.0f FPS", ImGui::GetIO().Framerate);
            ImGui::EndMainMenuBar();
        }

        // ── Full-window DockSpace ──────────────────────────────────────────────
        {
            const ImGuiViewport *vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGui::SetNextWindowViewport(vp->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            constexpr ImGuiWindowFlags dsFlags =
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove    |
                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                ImGuiWindowFlags_NoDocking  | ImGuiWindowFlags_NoBackground;
            ImGui::Begin("##DockspaceHost", nullptr, dsFlags);
            ImGui::PopStyleVar(3);

            const ImGuiID dockId = ImGui::GetID("MainDockSpace");
            ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

            // Build default layout once (first run only — overridden by saved .ini afterwards).
            static bool layoutBuilt = false;
            if (!layoutBuilt) {
                layoutBuilt = true;
                ImGui::DockBuilderRemoveNode(dockId);
                ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);

                ImGuiID left, mid;
                ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, 0.18f, &left, &mid);

                ImGuiID right, center;
                ImGui::DockBuilderSplitNode(mid, ImGuiDir_Right, 0.25f, &right, &center);

                ImGuiID viewport, bottom;
                ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, &bottom, &viewport);

                ImGui::DockBuilderDockWindow("Scene Hierarchy", left);
                ImGui::DockBuilderDockWindow("Viewport",        viewport);
                ImGui::DockBuilderDockWindow("Inspector",       right);
                ImGui::DockBuilderDockWindow("Render Settings", right);
                ImGui::DockBuilderDockWindow("Assets",          bottom);
                ImGui::DockBuilderFinish(dockId);
            }

            ImGui::End(); // DockspaceHost
        }

        // ── Viewport panel ─────────────────────────────────────────────────────
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport");
        {
            viewportFocused = ImGui::IsWindowFocused() || ImGui::IsWindowHovered();
            const ImVec2 sz = ImGui::GetContentRegionAvail();
            // Flip V so the OpenGL texture (bottom-left origin) displays right-side up.
            ImGui::Image(
                static_cast<ImTextureID>(static_cast<uintptr_t>(viewportTexId)),
                sz, ImVec2(0, 1), ImVec2(1, 0));

            // Viewport image rect — needed for picking and gizmo coordinate conversion.
            const ImVec2 vpMin  = ImGui::GetItemRectMin();
            const ImVec2 vpMax  = ImGui::GetItemRectMax();
            const ImVec2 vpSize = ImVec2(vpMax.x - vpMin.x, vpMax.y - vpMin.y);

            // ── Gizmo mode hot-keys (W/E/R) ───────────────────────────────────
            if (viewportFocused) {
                using K = sonnet::api::input::Key;
                if (input.isKeyJustPressed(K::W)) gizmoMode = GizmoMode::Translate;
                if (input.isKeyJustPressed(K::E)) gizmoMode = GizmoMode::Rotate;
                if (input.isKeyJustPressed(K::R)) gizmoMode = GizmoMode::Scale;
            }

            // ── Object picking (LMB click, not over gizmo) ────────────────────
            const bool lmbClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            if (lmbClicked && gizmoActiveAxis == 0) {
                const ImVec2 mp = ImGui::GetMousePos();
                const glm::vec2 uv{(mp.x - vpMin.x) / vpSize.x,
                                   (mp.y - vpMin.y) / vpSize.y};

                // Unproject click to a world-space ray.
                const glm::vec4 ndcNear{uv.x*2.f-1.f, (1.f-uv.y)*2.f-1.f, -1.f, 1.f};
                const glm::vec4 ndcFar {ndcNear.x,     ndcNear.y,           1.f,  1.f};
                const glm::mat4 invVP = glm::inverse(projMat * viewMat);
                auto unproj = [&](glm::vec4 ndc) {
                    glm::vec4 w = invVP * ndc;
                    return glm::vec3(w) / w.w;
                };
                const glm::vec3 rayOrig = camPos;
                const glm::vec3 rayDir  = glm::normalize(unproj(ndcFar) - unproj(ndcNear));

                // Helper: walk up hierarchy to find a selectable (no '/' in name) ancestor.
                auto selectableAncestor = [&](sonnet::world::GameObject *obj)
                        -> sonnet::world::GameObject * {
                    while (obj && obj->name.find('/') != std::string::npos) {
                        auto *p = obj->transform.getParent();
                        if (!p) break;
                        // Find the GameObject that owns this transform.
                        obj = nullptr;
                        for (auto &o : scene.objects())
                            if (&o->transform == p) { obj = o.get(); break; }
                    }
                    return obj;
                };

                // Ray–sphere test; pick the closest hit.
                float bestT = std::numeric_limits<float>::max();
                sonnet::world::GameObject *bestObj = nullptr;
                for (auto &o : scene.objects()) {
                    if (!o->render) continue;
                    const glm::vec3 center = o->transform.getWorldPosition();
                    const float     radius = glm::length(o->transform.getLocalScale()) * 0.6f;

                    // Analytic ray-sphere intersection.
                    const glm::vec3 oc = rayOrig - center;
                    const float     b  = glm::dot(oc, rayDir);
                    const float     c  = glm::dot(oc, oc) - radius * radius;
                    const float  disc  = b * b - c;
                    if (disc < 0.0f) continue;
                    const float t = -b - std::sqrt(disc);
                    if (t > 0.0f && t < bestT) { bestT = t; bestObj = o.get(); }
                }

                if (bestObj) {
                    auto *sel = selectableAncestor(bestObj);
                    selectedObject = sel ? sel : bestObj;
                    editEuler = glm::degrees(glm::eulerAngles(
                        selectedObject->transform.getLocalRotation()));
                } else {
                    // Click on empty space — deselect.
                    selectedObject = nullptr;
                }
            }

            // ── Transform gizmo ───────────────────────────────────────────────
            if (selectedObject && selectedObject != &cameraObj) {
                ImDrawList *dl = ImGui::GetWindowDrawList();

                const glm::vec3 origin = selectedObject->transform.getWorldPosition();
                const float dist   = glm::distance(camPos, origin);
                const float gLen   = dist * 0.15f;

                // Project a world point to an ImGui screen position.
                auto w2s = [&](glm::vec3 wp) -> std::optional<ImVec2> {
                    glm::vec4 clip = projMat * viewMat * glm::vec4(wp, 1.0f);
                    if (clip.w <= 0.0001f) return std::nullopt;
                    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    return ImVec2{vpMin.x + (ndc.x * 0.5f + 0.5f) * vpSize.x,
                                  vpMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * vpSize.y};
                };

                // Distance from point P to line segment AB (2D).
                auto ptSegDist = [](ImVec2 p, ImVec2 a, ImVec2 b) -> float {
                    const float dx = b.x-a.x, dy = b.y-a.y;
                    const float lenSq = dx*dx + dy*dy;
                    if (lenSq < 1e-6f) return std::hypot(p.x-a.x, p.y-a.y);
                    const float t = std::clamp(((p.x-a.x)*dx + (p.y-a.y)*dy) / lenSq, 0.f, 1.f);
                    return std::hypot(p.x - (a.x+t*dx), p.y - (a.y+t*dy));
                };

                const glm::vec3 axes[4] = {{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
                constexpr ImU32 axisColors[4] = {
                    0, IM_COL32(220,50,50,255), IM_COL32(50,220,50,255), IM_COL32(50,100,255,255)
                };
                constexpr ImU32 axisHover[4] = {
                    0, IM_COL32(255,120,120,255), IM_COL32(120,255,120,255), IM_COL32(120,160,255,255)
                };

                const ImVec2 mousePos = ImGui::GetMousePos();
                const bool   lmbDown  = ImGui::IsMouseDown(ImGuiMouseButton_Left);

                // ── End drag ──────────────────────────────────────────────────
                if (gizmoActiveAxis > 0 && !lmbDown)
                    gizmoActiveAxis = 0;

                // ── Hover detection (only when not dragging and not RMB) ──────
                gizmoHoverAxis = 0;
                if (gizmoActiveAxis == 0 &&
                    !input.isMouseDown(sonnet::api::input::MouseButton::Right)) {
                    if (auto os = w2s(origin)) {
                        for (int i = 1; i <= 3; ++i) {
                            if (auto ts = w2s(origin + axes[i] * gLen)) {
                                if (ptSegDist(mousePos, *os, *ts) < 8.0f)
                                    gizmoHoverAxis = i;
                            }
                        }
                        // For Rotate: check proximity to the projected circle center as well.
                        if (gizmoMode == GizmoMode::Rotate && gizmoHoverAxis == 0) {
                            for (int i = 1; i <= 3; ++i) {
                                if (auto ts = w2s(origin + axes[i] * gLen)) {
                                    // Distance from mouse to each axis tip circle.
                                    const float d = std::hypot(mousePos.x - ts->x,
                                                               mousePos.y - ts->y);
                                    if (d < 12.0f) gizmoHoverAxis = i;
                                }
                            }
                        }
                    }
                }

                // ── Start drag ────────────────────────────────────────────────
                if (lmbClicked && gizmoHoverAxis > 0) {
                    gizmoActiveAxis = gizmoHoverAxis;
                    dragStartPos    = selectedObject->transform.getWorldPosition();
                    dragStartRot    = selectedObject->transform.getLocalRotation();
                    dragStartScale  = selectedObject->transform.getLocalScale();
                    dragStartMouse  = mousePos;
                    dragAccum       = 0.0f;
                }

                // ── Apply drag ────────────────────────────────────────────────
                const int activeAxis = gizmoActiveAxis > 0 ? gizmoActiveAxis
                                                           : gizmoHoverAxis;
                if (gizmoActiveAxis > 0 && lmbDown) {
                    const glm::vec3 axisDir = axes[gizmoActiveAxis];

                    // Project axis to screen to find screen-space drag direction.
                    const auto os = w2s(origin);
                    const auto ts = w2s(origin + axisDir * gLen);
                    if (os && ts) {
                        const ImVec2 screenAxis{ts->x - os->x, ts->y - os->y};
                        const float  screenLen = std::hypot(screenAxis.x, screenAxis.y);
                        if (screenLen > 0.5f) {
                            const ImVec2 screenDir{screenAxis.x / screenLen,
                                                   screenAxis.y / screenLen};
                            const ImVec2 totalDelta{mousePos.x - dragStartMouse.x,
                                                    mousePos.y - dragStartMouse.y};
                            const float signed_px =
                                totalDelta.x * screenDir.x + totalDelta.y * screenDir.y;

                            if (gizmoMode == GizmoMode::Translate) {
                                const float sensitivity = dist * 0.0018f;
                                const glm::vec3 newPos =
                                    dragStartPos + axisDir * signed_px * sensitivity;
                                selectedObject->transform.setWorldPosition(newPos);
                                // Keep editEuler in sync.
                                editEuler = glm::degrees(glm::eulerAngles(
                                    selectedObject->transform.getLocalRotation()));
                            } else if (gizmoMode == GizmoMode::Rotate) {
                                dragAccum = signed_px * 0.5f; // degrees
                                const glm::quat delta = glm::angleAxis(
                                    glm::radians(dragAccum), axisDir);
                                selectedObject->transform.setLocalRotation(
                                    glm::normalize(delta * dragStartRot));
                                editEuler = glm::degrees(glm::eulerAngles(
                                    selectedObject->transform.getLocalRotation()));
                            } else { // Scale
                                const float factor = 1.0f + signed_px * 0.005f;
                                glm::vec3 newScale = dragStartScale;
                                newScale[gizmoActiveAxis - 1] *= std::max(0.001f, factor);
                                selectedObject->transform.setLocalScale(newScale);
                            }
                        }
                    }
                }

                // ── Draw gizmo ────────────────────────────────────────────────
                if (auto os = w2s(origin)) {
                    for (int i = 1; i <= 3; ++i) {
                        const ImU32 col = (i == activeAxis) ? axisHover[i] : axisColors[i];
                        const glm::vec3 tipWorld = origin + axes[i] * gLen;
                        const auto ts = w2s(tipWorld);
                        if (!ts) continue;

                        dl->AddLine(*os, *ts, col, 2.5f);

                        // Arrowhead / handle at tip.
                        const ImVec2 shaft{ts->x - os->x, ts->y - os->y};
                        const float  shaftLen = std::hypot(shaft.x, shaft.y);
                        if (shaftLen < 1.0f) continue;
                        const ImVec2 dir{shaft.x / shaftLen, shaft.y / shaftLen};
                        const ImVec2 perp{-dir.y, dir.x};
                        constexpr float kHeadLen = 10.0f, kHeadW = 5.0f;

                        if (gizmoMode == GizmoMode::Translate) {
                            const ImVec2 base{ts->x - dir.x*kHeadLen,
                                              ts->y - dir.y*kHeadLen};
                            dl->AddTriangleFilled(
                                *ts,
                                ImVec2{base.x + perp.x*kHeadW, base.y + perp.y*kHeadW},
                                ImVec2{base.x - perp.x*kHeadW, base.y - perp.y*kHeadW},
                                col);
                        } else if (gizmoMode == GizmoMode::Scale) {
                            constexpr float kSq = 5.0f;
                            dl->AddRectFilled(
                                ImVec2{ts->x - kSq, ts->y - kSq},
                                ImVec2{ts->x + kSq, ts->y + kSq}, col);
                        } else { // Rotate: small circle at tip
                            dl->AddCircleFilled(*ts, 5.0f, col);
                        }
                    }

                    // Origin dot.
                    dl->AddCircleFilled(*os, 4.0f, IM_COL32(220, 220, 220, 200));
                }
            }

            // ── Toolbar overlay (mode buttons + hint) ─────────────────────────
            {
                ImGui::SetCursorPos(ImVec2(8, 28));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImVec4(0.15f, 0.15f, 0.15f, 0.75f));

                auto modeBtn = [&](const char *label, GizmoMode mode, const char *key) {
                    const bool active = (gizmoMode == mode);
                    if (active)
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.9f, 0.9f));
                    if (ImGui::SmallButton((std::string(key) + " " + label).c_str()))
                        gizmoMode = mode;
                    if (active) ImGui::PopStyleColor();
                    ImGui::SameLine();
                };
                modeBtn("Translate", GizmoMode::Translate, "[W]");
                modeBtn("Rotate",    GizmoMode::Rotate,    "[E]");
                modeBtn("Scale",     GizmoMode::Scale,     "[R]");

                ImGui::PopStyleColor();
                ImGui::PopStyleVar();

                if (!input.isMouseDown(sonnet::api::input::MouseButton::Right) &&
                    !selectedObject) {
                    ImGui::SetCursorPos(ImVec2(8, 50));
                    ImGui::TextDisabled("Click to select  |  RMB + WASDQE to fly");
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();

        // ── Scene Hierarchy panel ─────────────────────────────────────────────
        ImGui::Begin("Scene Hierarchy");
        {
            // Build Transform* → GameObject* map for parent lookup.
            std::unordered_map<const sonnet::world::Transform *,
                               sonnet::world::GameObject *> tfToObj;
            for (auto &obj : scene.objects())
                tfToObj[&obj->transform] = obj.get();

            std::function<void(sonnet::world::GameObject &)> drawNode =
                [&](sonnet::world::GameObject &obj) {
                    std::vector<sonnet::world::GameObject *> childObjs;
                    for (auto *childTf : obj.transform.children()) {
                        auto it = tfToObj.find(childTf);
                        if (it != tfToObj.end())
                            childObjs.push_back(it->second);
                    }
                    ImGuiTreeNodeFlags flags =
                        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (childObjs.empty())
                        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    if (&obj == selectedObject)
                        flags |= ImGuiTreeNodeFlags_Selected;

                    const bool opened = ImGui::TreeNodeEx(obj.name.c_str(), flags);
                    if (ImGui::IsItemClicked()) {
                        if (selectedObject != &obj) {
                            selectedObject = &obj;
                            editEuler = glm::degrees(glm::eulerAngles(
                                obj.transform.getLocalRotation()));
                        }
                    }
                    if (opened && !childObjs.empty()) {
                        for (auto *child : childObjs) drawNode(*child);
                        ImGui::TreePop();
                    }
                };

            for (auto &obj : scene.objects())
                if (obj->transform.getParent() == nullptr)
                    drawNode(*obj);

            ImGui::Separator();
            if (ImGui::Button("Save Scene"))
                saveScene();
            ImGui::SameLine();
            ImGui::TextDisabled("persists transforms");
        }
        ImGui::End();

        // ── Inspector panel ───────────────────────────────────────────────────
        ImGui::Begin("Inspector");
        if (selectedObject) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
            ImGui::Text("%s", selectedObject->name.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();

            // ── Transform ─────────────────────────────────────────────────────
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                glm::vec3 pos = selectedObject->transform.getLocalPosition();
                if (ImGui::DragFloat3("Position", &pos.x, 0.01f))
                    selectedObject->transform.setLocalPosition(pos);

                if (ImGui::DragFloat3("Rotation", &editEuler.x, 0.5f))
                    selectedObject->transform.setLocalRotation(
                        glm::quat(glm::radians(editEuler)));

                glm::vec3 scl = selectedObject->transform.getLocalScale();
                if (ImGui::DragFloat3("Scale", &scl.x, 0.01f, 0.001f, 100.0f))
                    selectedObject->transform.setLocalScale(scl);
            }

            // ── Render component ───────────────────────────────────────────────
            if (selectedObject->render) {
                if (ImGui::CollapsingHeader("Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("Mesh handle:     %llu",
                        static_cast<unsigned long long>(selectedObject->render->mesh.value));
                    ImGui::TextDisabled("Material handle: %llu",
                        static_cast<unsigned long long>(
                            selectedObject->render->material.templateHandle().value));

                    // Editable PBR scalars for the Cube and Floor objects.
                    if (selectedObject == &cube) {
                        ImGui::SliderFloat("Metallic##cube",  &cubeMetallic,  0.0f, 1.0f);
                        ImGui::SliderFloat("Roughness##cube", &cubeRoughness, 0.0f, 1.0f);
                    } else if (selectedObject == &floor) {
                        ImGui::SliderFloat("Metallic##floor",  &floorMetallic,  0.0f, 1.0f);
                        ImGui::SliderFloat("Roughness##floor", &floorRoughness, 0.0f, 1.0f);
                    }
                }
            }

            // ── Camera component ───────────────────────────────────────────────
            if (selectedObject->camera) {
                if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::SliderFloat("FOV",  &selectedObject->camera->fov,  10.0f, 120.0f);
                    ImGui::SliderFloat("Near", &selectedObject->camera->near,  0.01f,  5.0f);
                    ImGui::SliderFloat("Far",  &selectedObject->camera->far,  10.0f, 500.0f);
                    const glm::vec3 p = selectedObject->transform.getWorldPosition();
                    ImGui::TextDisabled("World pos %.2f  %.2f  %.2f", p.x, p.y, p.z);
                }
            }

            // ── Animation component ────────────────────────────────────────────
            if (selectedObject->animationPlayer) {
                auto &ap = *selectedObject->animationPlayer;
                if (!ap.clips.empty() &&
                    ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::PushID("anim");
                    if (ap.clips.size() > 1) {
                        if (ImGui::BeginCombo("Clip", ap.clips[ap.currentClip].name.c_str())) {
                            for (int c = 0; c < static_cast<int>(ap.clips.size()); ++c) {
                                const bool sel = (c == ap.currentClip);
                                if (ImGui::Selectable(ap.clips[c].name.c_str(), sel)) {
                                    ap.currentClip = c;
                                    ap.time = 0.0f;
                                    ap.playing = true;
                                }
                                if (sel) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                    } else {
                        ImGui::TextDisabled("Clip: %s", ap.clips[0].name.c_str());
                    }
                    if (ImGui::Button(ap.playing ? "Pause" : "Play")) ap.playing = !ap.playing;
                    ImGui::SameLine();
                    if (ImGui::Button("Restart")) { ap.time = 0.0f; ap.playing = true; }
                    ImGui::SameLine();
                    ImGui::Checkbox("Loop", &ap.loop);
                    const float dur = ap.clips[ap.currentClip].duration;
                    ImGui::SliderFloat("Time", &ap.time, 0.0f, dur > 0.0f ? dur : 1.0f, "%.2f s");
                    ImGui::PopID();
                }
            }
        } else {
            ImGui::TextDisabled("Select an object in the Scene Hierarchy");
        }
        ImGui::End();

        // ── Assets panel ──────────────────────────────────────────────────────
        ImGui::Begin("Assets");
        if (ImGui::BeginTabBar("AssetTabs")) {
            auto listAssets = [](const char *label,
                                 const std::vector<std::string> &names,
                                 const char *icon) {
                if (ImGui::BeginTabItem(label)) {
                    ImGui::BeginChild("scroll");
                    for (const auto &n : names)
                        ImGui::TextUnformatted((std::string(icon) + "  " + n).c_str());
                    if (names.empty()) ImGui::TextDisabled("(none)");
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            };
            listAssets("Meshes",    assetMeshNames,     "[M]");
            listAssets("Materials", assetMaterialNames, "[MAT]");
            listAssets("Textures",  assetTextureNames,  "[TEX]");
            listAssets("Shaders",   assetShaderNames,   "[SHD]");
            ImGui::EndTabBar();
        }
        ImGui::End();

        // ── Render Settings panel ─────────────────────────────────────────────
        ImGui::Begin("Render Settings");

        if (ImGui::CollapsingHeader("Directional Light")) {
            ImGui::DragFloat3("Direction",      &lightDir.x,      0.01f, -1.0f, 1.0f);
            ImGui::ColorEdit3("Color##sun",     &lightColor.x);
            ImGui::SliderFloat("Intensity##sun",&lightIntensity,  0.0f,  4.0f);
        }

        if (ImGui::CollapsingHeader("Point Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
            int removeIdx = -1;
            for (int i = 0; i < static_cast<int>(pointLights.size()); ++i) {
                ImGui::PushID(i);
                auto &pl = pointLights[i];
                const std::string label = (i == 0) ? "Lamp" : "Light " + std::to_string(i);
                if (ImGui::CollapsingHeader(label.c_str())) {
                    ImGui::Checkbox("Enabled", &pl.enabled);
                    ImGui::ColorEdit3("Color##pl",        &pl.color.x);
                    ImGui::SliderFloat("Intensity##pl", &pl.intensity, 0.0f, 20.0f);
                    if (i == 0) {
                        const glm::vec3 p = lamp.transform.getWorldPosition();
                        ImGui::TextDisabled("Pos  %.2f  %.2f  %.2f", p.x, p.y, p.z);
                    } else {
                        ImGui::DragFloat3("Position##pl", &pl.position.x, 0.05f);
                        if (ImGui::Button("Remove")) removeIdx = i;
                    }
                }
                ImGui::PopID();
            }
            if (removeIdx >= 0)
                pointLights.erase(pointLights.begin() + removeIdx);
            if (static_cast<int>(pointLights.size()) < 8) {
                if (ImGui::Button("+ Add Point Light"))
                    pointLights.push_back({.color={1,1,1},.intensity=3,.position=camPos,.enabled=true});
            } else {
                ImGui::TextDisabled("Max 8 lights");
            }
        }

        if (ImGui::CollapsingHeader("Shadows")) {
            ImGui::SliderFloat("Dir bias",    &shadowBias,       0.0001f, 0.05f, "%.4f");
            ImGui::SliderFloat("Point bias",  &pointShadowBias,  0.001f,  0.05f, "%.4f");
            ImGui::TextDisabled("Shadow lights: %d / %d", shadowLightCount, MAX_SHADOW_LIGHTS);
            for (int c = 0; c < NUM_CASCADES; ++c)
                ImGui::TextDisabled("  Cascade %d split: %.2f m", c, csmSplitDepths[c]);
        }

        if (ImGui::CollapsingHeader("Tone-mapping")) {
            ImGui::SliderFloat("Exposure", &exposure, 0.1f, 5.0f);
        }

        if (ImGui::CollapsingHeader("Bloom")) {
            ImGui::SliderFloat("Threshold##bloom",  &bloomThreshold,  0.5f, 3.0f);
            ImGui::SliderFloat("Intensity##bloom",  &bloomIntensity,  0.0f, 3.0f);
            ImGui::SliderInt  ("Iterations##bloom", &bloomIterations, 1,    8);
        }

        if (ImGui::CollapsingHeader("SSAO")) {
            ImGui::Checkbox   ("Enable##ssao",  &ssaoEnabled);
            ImGui::Checkbox   ("Visualize AO",  &ssaoShow);
            ImGui::SliderFloat("Radius##ssao",  &ssaoRadius, 0.1f, 3.0f);
            ImGui::SliderFloat("Bias##ssao",    &ssaoBias,   0.01f, 0.2f, "%.3f");
        }

        if (ImGui::CollapsingHeader("SSR")) {
            ImGui::Checkbox   ("Enable##ssr",        &ssrEnabled);
            ImGui::SliderFloat("Strength##ssr",      &ssrStrength,     0.0f, 2.0f);
            ImGui::SliderInt  ("Max Steps##ssr",     &ssrMaxSteps,     8,    128);
            ImGui::SliderFloat("Step Size##ssr",     &ssrStepSize,     0.01f, 0.5f);
            ImGui::SliderFloat("Thickness##ssr",     &ssrThickness,    0.01f, 1.0f);
            ImGui::SliderFloat("Max Distance##ssr",  &ssrMaxDistance,  1.0f,  30.0f);
            ImGui::SliderFloat("Roughness Max##ssr", &ssrRoughnessMax, 0.0f,  1.0f);
        }

        if (ImGui::CollapsingHeader("Anti-aliasing")) {
            ImGui::Checkbox("FXAA", &fxaaEnabled);
        }

        if (ImGui::CollapsingHeader("Selection Outline")) {
            ImGui::Checkbox   ("Enable##outline", &outlineEnabled);
            ImGui::ColorEdit3 ("Color##outline",  &outlineColor.x);
        }

        if (ImGui::CollapsingHeader("Scene")) {
            ImGui::SliderFloat("Rotation speed", &rotationSpeed, 0.0f, 360.0f);
        }

        ImGui::End(); // Render Settings

        imgui.end();

        window.swapBuffers();
        input.nextFrame();
    }

    return 0;
}
