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
#include <random>
#include <string>
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
    window.captureCursor();

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

    // ── Normals pre-pass render target (RGBA16F color + Depth24 sampled) ─────
    const auto normalsRTHandle = renderer.createRenderTarget(sonnet::api::render::RenderTargetDesc{
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
        .depth  = sonnet::api::render::TextureAttachmentDesc{
            .format      = sonnet::api::render::TextureFormat::Depth24,
            .samplerDesc = {
                .minFilter = sonnet::api::render::MinFilter::Linear,
                .magFilter = sonnet::api::render::MagFilter::Linear,
                .wrapS     = sonnet::api::render::TextureWrap::ClampToEdge,
                .wrapT     = sonnet::api::render::TextureWrap::ClampToEdge,
            },
        },
    });
    const auto normalsTex      = renderer.colorTextureHandle(normalsRTHandle, 0);
    const auto normalsDepthTex = renderer.depthTextureHandle(normalsRTHandle);

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

    // Attach IBL + SSAO textures to every lit material instance.
    const float maxLOD = static_cast<float>(ibl.prefilteredLODs - 1);
    for (auto *mat : {&cubeMat, &floorMat}) {
        mat->addTexture("uIrradianceMap",  ibl.irradianceHandle);
        mat->addTexture("uPrefilteredMap", ibl.prefilteredHandle);
        mat->addTexture("uBRDFLUT",        ibl.brdfLUTHandle);
        mat->set("uMaxPrefilteredLOD", maxLOD);
        mat->addTexture("uSSAO",           ssaoBlurTex);
    }

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

    // ── Pre-pass shader and material (outputs view-space normals) ─────────────
    const auto prepassVertSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/prepass.vert");
    const auto prepassFragSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/prepass.frag");
    const auto prepassShader  = renderer.createShader(prepassVertSrc, prepassFragSrc);
    const auto prepassMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = prepassShader,
        .renderState  = {},
    });
    sonnet::api::render::MaterialInstance prepassMat{prepassMatTmpl};

    // ── SSAO shader and material ───────────────────────────────────────────────
    const auto ssaoVertSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/ssao.vert");
    const auto ssaoFragSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/ssao.frag");
    const auto ssaoShader  = renderer.createShader(ssaoVertSrc, ssaoFragSrc);
    const auto ssaoMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = ssaoShader,
        .renderState  = noDepthState,
    });
    sonnet::api::render::MaterialInstance ssaoMat{ssaoMatTmpl};
    ssaoMat.addTexture("uNormalMap", normalsTex);
    ssaoMat.addTexture("uDepthMap",  normalsDepthTex);
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

    // ── Skybox ────────────────────────────────────────────────────────────────
    const auto skyVertSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/sky.vert");
    const auto skyFragSrc = sonnet::loaders::ShaderLoader::load(DEMO_ASSETS_DIR "/shaders/sky.frag");
    const auto skyShader  = renderer.createShader(skyVertSrc, skyFragSrc);
    const auto skyMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = skyShader,
        .renderState  = {
            .depthTest  = true,
            .depthWrite = false,
            .depthFunc  = sonnet::api::render::DepthFunction::LessEqual,
            .cull       = sonnet::api::render::CullMode::None,
        },
    });
    sonnet::api::render::MaterialInstance skyMat{skyMatTmpl};
    skyMat.addTexture("uEnvMap", ibl.equirectHandle);

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
    float     ssaoRadius      = 0.5f;
    float     ssaoBias        = 0.025f;
    bool      uiMode          = false;

    // Per-object PBR scalar multipliers — applied on top of the ORM texture.
    // 1.0 = let the texture drive everything.
    float cubeMetallic      = 1.0f;
    float cubeRoughness     = 1.0f;
    float floorMetallic     = 1.0f;
    float floorRoughness    = 1.0f;
    glm::vec3 lampColor     = {1.0f, 0.75f, 0.3f};
    float     lampStrength  = 6.0f;

    float  rotation = 0.0f;
    double prevTime = glfwGetTime();

    while (!window.shouldClose()) {
        const double now = glfwGetTime();
        const float  dt  = static_cast<float>(now - prevTime);
        prevTime = now;

        window.pollEvents();

        if (input.isKeyJustPressed(sonnet::api::input::Key::Escape))
            window.requestClose();

        if (input.isKeyJustPressed(sonnet::api::input::Key::Tab)) {
            uiMode = !uiMode;
            if (uiMode) window.releaseCursor();
            else        window.captureCursor();
        }

        const auto fbSize = window.getFrameBufferSize();

        if (!uiMode)
            flyCamera.update(dt, input);

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

        // ── Pass 1.5: geometry pre-pass → normalsRT ────────────────────────────
        renderer.bindRenderTarget(normalsRTHandle);
        backend.setViewport(fbSize.x, fbSize.y);
        glDepthMask(GL_TRUE);
        backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}}, .depth = 1.0f });

        {
            sonnet::api::render::FrameContext preCtx{
                .viewMatrix       = viewMat,
                .projectionMatrix = projMat,
                .viewPosition     = camPos,
                .viewportWidth    = fbSize.x,
                .viewportHeight   = fbSize.y,
                .deltaTime        = 0.0f,
            };
            std::vector<sonnet::api::render::RenderItem> preQueue;
            for (const auto &obj : scene.objects()) {
                if (!obj->render) continue;
                preQueue.push_back({
                    .mesh        = obj->render->mesh,
                    .material    = prepassMat,
                    .modelMatrix = obj->transform.getModelMatrix(),
                });
            }
            renderer.beginFrame();
            renderer.render(preCtx, preQueue);
            renderer.endFrame();
        }

        // ── Pass 1.6: SSAO → ssaoRT (or clear to white if disabled) ──────────
        if (ssaoEnabled) {
            renderer.bindRenderTarget(ssaoRTHandle);
            backend.setViewport(fbSize.x, fbSize.y);
            backend.clear({ .colors = {{0, {1.0f, 1.0f, 1.0f, 1.0f}}} });
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

        // ── Pass 2: HDR scene ──────────────────────────────────────────────────
        renderer.bindRenderTarget(hdrRTHandle);
        backend.setViewport(fbSize.x, fbSize.y);
        backend.clear({
            .colors = {{0, {0.1f, 0.1f, 0.15f, 1.0f}}},
            .depth  = 1.0f,
        });

        cubeMat.set("uShadowBias",  shadowBias);
        cubeMat.set("uMetallic",    cubeMetallic);
        cubeMat.set("uRoughness",   cubeRoughness);
        floorMat.set("uShadowBias", shadowBias);
        floorMat.set("uMetallic",   floorMetallic);
        floorMat.set("uRoughness",  floorRoughness);
        lampMat.set("uEmissiveColor",    lampColor);
        lampMat.set("uEmissiveStrength", lampStrength);

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
            .lightSpaceMatrix = lightSpaceMat,
        };

        std::vector<sonnet::api::render::RenderItem> queue;
        scene.buildRenderQueue(queue);

        // ── Skybox item (drawn at depth=1.0 using LessEqual depth test) ──────
        std::vector<sonnet::api::render::RenderItem> skyQueue{{
            .mesh        = quadMeshHandle,
            .material    = skyMat,
            .modelMatrix = glm::mat4{1.0f},
        }};

        renderer.beginFrame();
        renderer.render(ctx, queue);      // opaque geometry first
        renderer.render(ctx, skyQueue);   // sky fills depth=1.0 regions
        renderer.endFrame();

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

        // ── Pass 3: tone-map HDR -> default framebuffer ───────────────────────
        backend.bindDefaultRenderTarget();
        backend.setViewport(fbSize.x, fbSize.y);
        backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });

        tonemapMat.set("uExposure",       exposure);
        tonemapMat.set("uBloomIntensity", bloomIntensity);

        fullscreenQuad(tonemapMat);

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

            if (ImGui::CollapsingHeader("Lamp", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::ColorEdit3("Color##lamp",     &lampColor.x);
                ImGui::SliderFloat("Strength##lamp", &lampStrength, 0.0f, 20.0f);
            }

            if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("Threshold##bloom",  &bloomThreshold,  0.5f, 3.0f);
                ImGui::SliderFloat("Intensity##bloom",  &bloomIntensity,  0.0f, 3.0f);
                ImGui::SliderInt  ("Iterations##bloom", &bloomIterations, 1,    8);
            }

            if (ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox   ("Enable##ssao",  &ssaoEnabled);
                ImGui::SliderFloat("Radius##ssao",  &ssaoRadius, 0.1f, 1.0f);
                ImGui::SliderFloat("Bias##ssao",    &ssaoBias,   0.005f, 0.1f, "%.3f");
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
        imgui.end();

        window.swapBuffers();
        input.nextFrame();
    }

    return 0;
}
