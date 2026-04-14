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

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
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

    // ── Shadow map render target (depth texture, no colour) ───────────────────
    constexpr std::uint32_t SHADOW_SIZE = 2048;
    const auto shadowRTHandle = renderer.createRenderTarget(sonnet::api::render::RenderTargetDesc{
        .width  = SHADOW_SIZE,
        .height = SHADOW_SIZE,
        .colors = {},
        .depth  = sonnet::api::render::TextureAttachmentDesc{
            .format      = sonnet::api::render::TextureFormat::Depth24,
            .samplerDesc = {
                .minFilter    = sonnet::api::render::MinFilter::Linear,
                .magFilter    = sonnet::api::render::MagFilter::Linear,
                .wrapS        = sonnet::api::render::TextureWrap::ClampToEdge,
                .wrapT        = sonnet::api::render::TextureWrap::ClampToEdge,
                .depthCompare = true,
            },
        },
    });
    const auto shadowDepthHandle = renderer.depthTextureHandle(shadowRTHandle);

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
    sceneLoader.registerTexture("shadowDepth", shadowDepthHandle);
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

    sonnet::api::render::MaterialInstance tonemapMat{tonemapMatTmpl};
    tonemapMat.addTexture("uHdrColor",      hdrTexHandle);
    tonemapMat.addTexture("uBloomTexture",  bloomBrightTex); // final blur result ends up here

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
    deferredMat.addTexture("uShadowMap",       shadowDepthHandle);
    deferredMat.addTexture("uIrradianceMap",   ibl.irradianceHandle);
    deferredMat.addTexture("uPrefilteredMap",  ibl.prefilteredHandle);
    deferredMat.addTexture("uBRDFLUT",         ibl.brdfLUTHandle);
    deferredMat.addTexture("uSSAO",            ssaoBlurTex);
    deferredMat.set("uMaxPrefilteredLOD", maxLOD);

    FlyCamera flyCamera{cameraObj.transform};

    // Tweakable state exposed via ImGui.
    float     rotationSpeed  = 45.0f;
    glm::vec3 lightDir       = {0.6f, 1.0f, 0.4f};
    glm::vec3 lightColor     = {1.0f, 1.0f, 1.0f};
    float     lightIntensity = 1.0f;
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
    bool      uiMode          = false;

    // Per-object PBR scalar multipliers — applied on top of the ORM texture.
    // 1.0 = let the texture drive everything.
    float cubeMetallic      = 1.0f;
    float cubeRoughness     = 1.0f;
    float floorMetallic     = 1.0f;
    float floorRoughness    = 1.0f;

    // ── Editable point lights ─────────────────────────────────────────────────
    // Light 0 is the lamp sphere — position tracks lamp.transform, color/strength
    // also drive the emissive material.  Lights 1-7 are freely placed.
    struct PointLightEdit {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float     intensity{3.0f};
        glm::vec3 position{0.0f}; // ignored for light 0 (uses lamp transform)
        bool      enabled{true};
    };
    std::vector<PointLightEdit> pointLights;
    // Light 0 — lamp sphere (position auto-synced to lamp transform)
    pointLights.push_back({ .color = {1.0f, 0.75f, 0.3f}, .intensity = 6.0f });
    // Light 1 — cool blue near the helmet
    pointLights.push_back({ .color = {0.3f, 0.5f, 1.0f}, .intensity = 4.0f,
                             .position = {-2.5f, 1.2f, 1.5f} });
    // Light 2 — warm red on the right, behind the cube arm
    pointLights.push_back({ .color = {1.0f, 0.2f, 0.1f}, .intensity = 4.0f,
                             .position = { 2.5f, 0.8f, 1.0f} });
    // Light 3 — teal below and behind, grazes the floor
    pointLights.push_back({ .color = {0.1f, 0.9f, 0.7f}, .intensity = 3.0f,
                             .position = { 0.0f, 0.1f, -2.0f} });

    float  rotation = 0.0f;
    double prevTime = glfwGetTime();

    // Scene hierarchy selection state.
    sonnet::world::GameObject *selectedObject = nullptr;
    glm::vec3                  editEuler{0.0f}; // Euler angles (degrees) for selected object

    while (!window.shouldClose()) {
        const double now = glfwGetTime();
        const float  dt  = static_cast<float>(now - prevTime);
        prevTime = now;

        window.pollEvents();

        if (input.isKeyJustPressed(sonnet::api::input::Key::Escape))
            window.requestClose();

        if (input.isKeyJustPressed(sonnet::api::input::Key::Tab))
            uiMode = !uiMode;

        const auto fbSize = window.getFrameBufferSize();

        if (uiMode) {
            window.releaseCursor();
        } else if (input.isMouseDown(sonnet::api::input::MouseButton::Right)) {
            window.captureCursor();
            flyCamera.update(dt, input);
        }

        rotation += rotationSpeed * dt;
        // Arm orbits around Y — the cube follows as a child.
        arm.transform.setLocalRotation(
            glm::angleAxis(glm::radians(rotation), glm::vec3{0, 1, 0}));
        // Cube also self-spins on its local X axis.
        cube.transform.setLocalRotation(
            glm::angleAxis(glm::radians(rotation * 0.5f), glm::vec3{1, 0, 0}));

        // ── Light-space matrix (orthographic projection from the light) ────────
        const glm::vec3 lightDirNorm = glm::normalize(lightDir);
        const glm::vec3 lightUp = std::abs(lightDirNorm.y) > 0.99f
                                ? glm::vec3{0.0f, 0.0f, 1.0f}
                                : glm::vec3{0.0f, 1.0f, 0.0f};
        const glm::mat4 lightView = glm::lookAt(lightDirNorm * 10.0f,
                                                glm::vec3{0.0f},
                                                lightUp);
        const glm::mat4 lightProj = glm::ortho(-4.0f, 4.0f, -4.0f, 4.0f, 1.0f, 20.0f);
        const glm::mat4 lightSpaceMat = lightProj * lightView;

        // ── Pass 1: shadow map ─────────────────────────────────────────────────
        renderer.bindRenderTarget(shadowRTHandle);
        backend.setViewport(SHADOW_SIZE, SHADOW_SIZE);
        backend.clear({ .depth = 1.0f });

        const glm::vec3 shadowOrigin{0.0f};
        sonnet::api::render::FrameContext shadowCtx{
            .viewMatrix       = lightView,
            .projectionMatrix = lightProj,
            .viewPosition     = shadowOrigin,
            .viewportWidth    = SHADOW_SIZE,
            .viewportHeight   = SHADOW_SIZE,
            .deltaTime        = 0.0f,
        };
        // Build shadow queue: same geometry, shadow-only material.
        std::vector<sonnet::api::render::RenderItem> shadowQueue;
        for (const auto &obj : scene.objects()) {
            if (!obj->render) continue;
            shadowQueue.push_back({
                .mesh        = obj->render->mesh,
                .material    = shadowMat,
                .modelMatrix = obj->transform.getModelMatrix(),
            });
        }
        renderer.beginFrame();
        renderer.render(shadowCtx, shadowQueue);
        renderer.endFrame();

        // ── Camera matrices (shared across pre-pass, SSAO, and lit pass) ──────
        const float aspect = fbSize.x > 0 && fbSize.y > 0
            ? static_cast<float>(fbSize.x) / static_cast<float>(fbSize.y)
            : 16.0f / 9.0f;
        const glm::mat4 viewMat    = cameraObj.camera->viewMatrix(cameraObj.transform);
        const glm::mat4 projMat    = cameraObj.camera->projectionMatrix(aspect);
        const glm::mat4 invProjMat = glm::inverse(projMat);
        const glm::vec3 camPos     = cameraObj.transform.getWorldPosition();

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
            .lightSpaceMatrix = lightSpaceMat,
        };

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

        if (ssaoShow) {
            // ── Debug: show raw SSAO buffer as grayscale ─────────────────────
            backend.bindDefaultRenderTarget();
            backend.setViewport(fbSize.x, fbSize.y);
            fullscreenQuad(ssaoShowMat);
        } else if (fxaaEnabled) {
            // ── Pass 3: tone-map HDR -> LDR RT ───────────────────────────────
            renderer.bindRenderTarget(ldrRT);
            backend.setViewport(fbSize.x, fbSize.y);
            backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
            fullscreenQuad(tonemapMat);

            // ── Pass 4: FXAA LDR RT -> default framebuffer ───────────────────
            backend.bindDefaultRenderTarget();
            backend.setViewport(fbSize.x, fbSize.y);
            fxaaMat.set("uTexelSize", glm::vec2(1.0f / fbSize.x, 1.0f / fbSize.y));
            fullscreenQuad(fxaaMat);
        } else {
            // ── Pass 3 (no FXAA): tone-map HDR directly -> default framebuffer
            backend.bindDefaultRenderTarget();
            backend.setViewport(fbSize.x, fbSize.y);
            fullscreenQuad(tonemapMat);
        }

        // ── ImGui ──────────────────────────────────────────────────────────────
        imgui.begin();
        if (uiMode) {
            ImGui::Begin("Debug  [Tab to close]");

            if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("Direction",       &lightDir.x,     0.01f, -1.0f, 1.0f);
                ImGui::ColorEdit3("Color",           &lightColor.x);
                ImGui::SliderFloat("Intensity##light",&lightIntensity, 0.0f,  4.0f);
            }

            if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("Bias", &shadowBias, 0.0001f, 0.05f, "%.4f");
            }

            if (ImGui::CollapsingHeader("Tone-mapping", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("Exposure", &exposure, 0.1f, 5.0f);
            }

            if (ImGui::CollapsingHeader("Point Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
                int removeIdx = -1;
                for (int i = 0; i < static_cast<int>(pointLights.size()); ++i) {
                    ImGui::PushID(i);
                    auto &pl = pointLights[i];

                    // Collapsing header per light.
                    const std::string label = (i == 0)
                        ? "Lamp (light 0)"
                        : "Light " + std::to_string(i);
                    bool open = ImGui::CollapsingHeader(label.c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen);
                    if (open) {
                        ImGui::Checkbox("Enabled", &pl.enabled);
                        ImGui::ColorEdit3("Color",        &pl.color.x);
                        ImGui::SliderFloat("Intensity", &pl.intensity, 0.0f, 20.0f);
                        if (i == 0) {
                            // Lamp sphere position: read-only display.
                            const glm::vec3 p = lamp.transform.getWorldPosition();
                            ImGui::TextDisabled("Position  %.2f  %.2f  %.2f", p.x, p.y, p.z);
                        } else {
                            ImGui::DragFloat3("Position", &pl.position.x, 0.05f);
                            if (ImGui::Button("Remove")) removeIdx = i;
                        }
                    }
                    ImGui::PopID();
                }
                if (removeIdx >= 0)
                    pointLights.erase(pointLights.begin() + removeIdx);

                if (static_cast<int>(pointLights.size()) < 8) {
                    if (ImGui::Button("+ Add Light")) {
                        pointLights.push_back({
                            .color    = {1.0f, 1.0f, 1.0f},
                            .intensity = 3.0f,
                            .position  = camPos, // spawn at camera position
                            .enabled   = true,
                        });
                    }
                } else {
                    ImGui::TextDisabled("Maximum 8 lights reached.");
                }
            }

            if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("Threshold##bloom",  &bloomThreshold,  0.5f, 3.0f);
                ImGui::SliderFloat("Intensity##bloom",  &bloomIntensity,  0.0f, 3.0f);
                ImGui::SliderInt  ("Iterations##bloom", &bloomIterations, 1,    8);
            }

            if (ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox   ("Enable##ssao",  &ssaoEnabled);
                ImGui::Checkbox   ("Visualize AO",  &ssaoShow);
                ImGui::SliderFloat("Radius##ssao",  &ssaoRadius, 0.1f, 3.0f);
                ImGui::SliderFloat("Bias##ssao",    &ssaoBias,   0.01f, 0.2f, "%.3f");
            }

            if (ImGui::CollapsingHeader("Anti-aliasing", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("FXAA", &fxaaEnabled);
                ImGui::TextDisabled("Look at diagonal edges (helmet, cube corners)");
            }

            if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("Cube");
                ImGui::SliderFloat("Metallic##cube",  &cubeMetallic,  0.0f, 1.0f);
                ImGui::SliderFloat("Roughness##cube", &cubeRoughness, 0.0f, 1.0f);
                ImGui::Spacing();
                ImGui::Text("Floor");
                ImGui::SliderFloat("Metallic##floor",  &floorMetallic,  0.0f, 1.0f);
                ImGui::SliderFloat("Roughness##floor", &floorRoughness, 0.0f, 1.0f);
            }

            if (ImGui::CollapsingHeader("Object", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("Rotation speed (deg/s)", &rotationSpeed, 0.0f, 360.0f);
            }

            if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                const glm::vec3 p = cameraObj.transform.getWorldPosition();
                ImGui::Text("Position  %.2f  %.2f  %.2f", p.x, p.y, p.z);
                ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
            }

            ImGui::End();
        }

        // ── Scene Hierarchy window ─────────────────────────────────────────────
        ImGui::Begin("Scene Hierarchy");

        // Build a map from Transform* -> GameObject* for parent lookup.
        std::unordered_map<const sonnet::world::Transform *, sonnet::world::GameObject *> tfToObj;
        for (auto &obj : scene.objects())
            tfToObj[&obj->transform] = obj.get();

        // Recursive node draw.
        std::function<void(sonnet::world::GameObject &)> drawNode =
            [&](sonnet::world::GameObject &obj) {
                // Collect child GameObjects (transform children that are scene objects).
                std::vector<sonnet::world::GameObject *> childObjs;
                for (auto *childTf : obj.transform.children()) {
                    auto it = tfToObj.find(childTf);
                    if (it != tfToObj.end())
                        childObjs.push_back(it->second);
                }

                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                           ImGuiTreeNodeFlags_SpanAvailWidth;
                if (childObjs.empty())
                    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (&obj == selectedObject)
                    flags |= ImGuiTreeNodeFlags_Selected;

                bool opened = ImGui::TreeNodeEx(obj.name.c_str(), flags);
                if (ImGui::IsItemClicked()) {
                    if (selectedObject != &obj) {
                        selectedObject = &obj;
                        editEuler = glm::degrees(glm::eulerAngles(
                            obj.transform.getLocalRotation()));
                    }
                }
                if (opened && !childObjs.empty()) {
                    for (auto *child : childObjs)
                        drawNode(*child);
                    ImGui::TreePop();
                }
            };

        // Draw only root objects (no transform parent).
        for (auto &obj : scene.objects()) {
            if (obj->transform.getParent() == nullptr)
                drawNode(*obj);
        }

        // Transform editor for selected object.
        if (selectedObject) {
            ImGui::Separator();
            ImGui::TextDisabled("%s", selectedObject->name.c_str());

            glm::vec3 pos = selectedObject->transform.getLocalPosition();
            if (ImGui::DragFloat3("Position##hier", &pos.x, 0.01f))
                selectedObject->transform.setLocalPosition(pos);

            if (ImGui::DragFloat3("Rotation##hier", &editEuler.x, 0.5f))
                selectedObject->transform.setLocalRotation(
                    glm::quat(glm::radians(editEuler)));

            glm::vec3 scl = selectedObject->transform.getLocalScale();
            if (ImGui::DragFloat3("Scale##hier", &scl.x, 0.01f, 0.001f, 100.0f))
                selectedObject->transform.setLocalScale(scl);
        }

        ImGui::End();

        imgui.end();

        window.swapBuffers();
        input.nextFrame();
    }

    return 0;
}
