// Sonnet v2 — Demo application
// Refactored: init + frame loop only; all subsystems live in their own files.

#include "EditorUI.h"
#include "FlyCamera.h"
#include "IBL.h"
#include "PostProcess.h"
#include "RenderTargets.h"
#include "ShaderRegistry.h"
#include "ShadowMaps.h"

#include <sonnet/api/render/FrameContext.h>
#include <sonnet/api/render/Light.h>
#include <sonnet/input/InputSystem.h>
#include <sonnet/renderer/frontend/BackendFactory.h>
#include <sonnet/renderer/frontend/Renderer.h>
#include <sonnet/physics/PhysicsSystem.h>
#include <sonnet/scene/SceneLoader.h>
#include <sonnet/scripting/LuaScriptRuntime.h>
#include <sonnet/ui/ImGuiLayer.h>
#if defined(SONNET_USE_VULKAN)
#  include <sonnet/renderer/vulkan/VkRendererBackend.h>
#endif
#include <sonnet/window/GLFWInputAdapter.h>
#include <sonnet/window/GLFWWindow.h>
#include <sonnet/world/Scene.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <random>
#include <string>
#include <vector>

int main() {
    // ── Window / input / backend / ImGui ─────────────────────────────────────
    sonnet::window::GLFWWindow window{{1280, 720, "Sonnet v2 Demo"}};
    sonnet::input::InputSystem input;
    sonnet::window::GLFWInputAdapter adapter{input};
    window.setInputAdapter(&adapter);

    auto  backendPtr = sonnet::renderer::frontend::makeBackend(window);
    auto &backend    = *backendPtr;
    backend.initialize();

    sonnet::ui::ImGuiLayer imgui;
#if defined(SONNET_USE_VULKAN)
    {
        auto *vkBackend = static_cast<sonnet::renderer::vulkan::VkRendererBackend *>(backendPtr.get());
        const auto info = vkBackend->imGuiInitInfo();
        imgui.init(window.handle(), sonnet::ui::VulkanInitInfo{
            .instance       = info.instance,
            .physicalDevice = info.physicalDevice,
            .device         = info.device,
            .queueFamily    = info.queueFamily,
            .queue          = info.queue,
            .renderPass     = info.renderPass,
            .minImageCount  = info.minImageCount,
            .imageCount     = info.imageCount,
            .descriptorPool = info.descriptorPool,
        });
    }
#else
    imgui.init(window.handle());
#endif

    sonnet::renderer::frontend::Renderer renderer{backend};

    // ── Shader registry ───────────────────────────────────────────────────────
    ShaderRegistry registry{renderer};

    // ── Shadow maps ───────────────────────────────────────────────────────────
    ShadowMaps shadows{renderer, backend,
        registry.compile(DEMO_ASSETS_DIR "/shaders/shadow.vert",
                         DEMO_ASSETS_DIR "/shaders/shadow.frag"),
        registry.compile(DEMO_ASSETS_DIR "/shaders/point_shadow.vert",
                         DEMO_ASSETS_DIR "/shaders/point_shadow.frag")};

    // ── IBL — irradiance, pre-filtered specular, BRDF LUT ────────────────────
    const IBLMaps ibl = buildIBL(renderer, backend,
        DEMO_ASSETS_DIR "/kloppenheim_06_1k.hdr",
        DEMO_ASSETS_DIR "/shaders");

    // ── Physics ───────────────────────────────────────────────────────────────
    sonnet::physics::PhysicsSystem physics;
    physics.init();

    // ── Scene ─────────────────────────────────────────────────────────────────
    sonnet::world::Scene scene;
    sonnet::scene::SceneLoader sceneLoader;
    const auto loaded = sceneLoader.load(
        DEMO_ASSETS_DIR "/scene.json", DEMO_ASSETS_DIR, scene, renderer, &physics);

    // ── Script runtime ────────────────────────────────────────────────────────
    sonnet::scripting::LuaScriptRuntime scriptRuntime;
    {
        std::ifstream sceneFile{DEMO_ASSETS_DIR "/scene.json"};
        if (sceneFile) {
            auto doc = nlohmann::json::parse(sceneFile, nullptr, false);
            if (!doc.is_discarded() && doc.contains("objects")) {
                for (const auto &jobj : doc["objects"]) {
                    if (!jobj.contains("script") || !jobj.contains("name")) continue;
                    const std::string objName    = jobj["name"].get<std::string>();
                    const std::string scriptPath = std::string(DEMO_ASSETS_DIR "/")
                                                 + jobj["script"].get<std::string>();
                    auto it = loaded.objects.find(objName);
                    if (it != loaded.objects.end())
                        scriptRuntime.attachScript(*it->second, scriptPath);
                }
            }
        }
    }
    scriptRuntime.init(scene, input);

    // Named object references used in the frame loop.
    auto &arm       = *loaded.objects.at("Arm");
    auto &cameraObj = *loaded.objects.at("Camera");
    auto &lamp      = *loaded.objects.at("Lamp");

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
    // Packed as RGBA32F (alpha unused by ssao.frag) so we use a format the
    // engine's TextureFormat enum supports — no need for a raw-GL wrapper.
    sonnet::core::GPUTextureHandle ssaoNoiseHandle{};
    {
        std::mt19937 rng(123);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        std::vector<float> noiseData(16 * 4);
        for (std::size_t i = 0; i < 16; ++i) {
            noiseData[i * 4 + 0] = dist(rng) * 2.0f - 1.0f;
            noiseData[i * 4 + 1] = dist(rng) * 2.0f - 1.0f;
            noiseData[i * 4 + 2] = 0.0f;
            noiseData[i * 4 + 3] = 0.0f;
        }
        const auto *bytes    = reinterpret_cast<const std::byte *>(noiseData.data());
        const std::size_t nB = noiseData.size() * sizeof(float);

        sonnet::api::render::TextureDesc tdesc{
            .size       = {4, 4},
            .format     = sonnet::api::render::TextureFormat::RGBA32F,
            .type       = sonnet::api::render::TextureType::Texture2D,
            .usageFlags = sonnet::api::render::TextureUsage::Sampled,
            .colorSpace = sonnet::api::render::ColorSpace::Linear,
            .useMipmaps = false,
        };
        sonnet::api::render::SamplerDesc sdesc{
            .minFilter = sonnet::api::render::MinFilter::Nearest,
            .magFilter = sonnet::api::render::MagFilter::Nearest,
            .wrapS     = sonnet::api::render::TextureWrap::Repeat,
            .wrapT     = sonnet::api::render::TextureWrap::Repeat,
            .wrapR     = sonnet::api::render::TextureWrap::Repeat,
        };
        sonnet::api::render::CPUTextureBuffer cpu{
            .width    = 4,
            .height   = 4,
            .channels = 4,
            .texels   = sonnet::core::Texels{bytes, nB},
        };
        ssaoNoiseHandle = renderer.createTexture(tdesc, sdesc, cpu);
    }

    // ── Render targets ────────────────────────────────────────────────────────
    const auto fbSize0 = window.getFrameBufferSize();
    RenderTargets rts{renderer,
        static_cast<std::uint32_t>(fbSize0.x),
        static_cast<std::uint32_t>(fbSize0.y),
        ssaoKernel, ssaoNoiseHandle};

    // ── Post-process pipeline ─────────────────────────────────────────────────
    const auto sphereMeshHandle    = lamp.render->mesh;
    const auto emissiveMatTemplate = lamp.render->material.templateHandle();
    PostProcess pp{renderer, backend, registry, rts, shadows, ibl,
                   rts.quadMesh, sphereMeshHandle, emissiveMatTemplate};

    // ── Camera + editor UI ────────────────────────────────────────────────────
    FlyCamera flyCamera{cameraObj.transform};
    EditorUI  ui{renderer, backend, scene, scriptRuntime, loaded, pp,
                 physics, DEMO_ASSETS_DIR "/scene.json"};

    // ── Tweakable render state (written through by EditorUI) ──────────────────
    float     rotationSpeed  = 45.0f;
    float     exposure        = 1.0f;
    float     shadowBias      = 0.005f;
    float     pointShadowBias = 0.008f;
    float     bloomThreshold  = 0.8f;
    float     bloomIntensity  = 0.5f;
    int       bloomIterations = 3;
    bool      ssaoEnabled     = true;
    float     ssaoRadius      = 1.5f;
    float     ssaoBias        = 0.05f;
    bool      ssaoShow        = false;
    bool      fxaaEnabled     = true;
    bool      outlineEnabled  = true;
    glm::vec3 outlineColor{1.0f, 0.6f, 0.05f};
    bool      ssrEnabled      = true;
    int       ssrMaxSteps     = 64;
    float     ssrStepSize     = 0.1f;
    float     ssrThickness    = 0.2f;
    float     ssrMaxDistance  = 10.0f;
    float     ssrRoughnessMax = 0.4f;
    float     ssrStrength     = 1.0f;

    float  rotation = 0.0f;
    double prevTime = glfwGetTime();
    auto   lastFbSize = fbSize0;

    std::string shaderReloadMsg;
    float       shaderReloadMsgTimer = 0.0f;

    ImTextureID viewportTexId = static_cast<ImTextureID>(
        renderer.imGuiTextureId(rts.viewportTex));

    // ── Frame loop ────────────────────────────────────────────────────────────
    while (!window.shouldClose()) {
        const double now = glfwGetTime();
        const float  dt  = static_cast<float>(now - prevTime);
        prevTime = now;

        window.pollEvents();

        // ── Shader + script hot-reload (0.5 s poll via registry) ─────────────
        if (const auto msg = registry.tick(dt); !msg.empty()) {
            shaderReloadMsg      = msg;
            shaderReloadMsgTimer = msg.rfind("Error", 0) == 0 ? 6.0f : 3.0f;
        }
        if (const auto msg = scriptRuntime.reload(); !msg.empty()) {
            shaderReloadMsg      = msg;
            shaderReloadMsgTimer = msg.rfind("Error", 0) == 0 ? 6.0f : 3.0f;
        }
        shaderReloadMsgTimer -= dt;

        if (input.isKeyJustPressed(sonnet::api::input::Key::Escape))
            window.requestClose();

        // ── Resize render targets when the window size changes ────────────────
        const auto fbSize = window.getFrameBufferSize();
        if (fbSize != lastFbSize && fbSize.x > 0 && fbSize.y > 0) {
            lastFbSize = fbSize;
            rts.resize(static_cast<std::uint32_t>(fbSize.x),
                       static_cast<std::uint32_t>(fbSize.y));
            viewportTexId = static_cast<ImTextureID>(
                renderer.imGuiTextureId(rts.viewportTex));
        }

        // ── Camera (RMB held + viewport focused) ──────────────────────────────
        if (ui.viewportFocused() &&
            input.isMouseDown(sonnet::api::input::MouseButton::Right)) {
            window.captureCursor();
            flyCamera.update(dt, input);
        } else {
            window.releaseCursor();
        }

        // ── Scene animation ───────────────────────────────────────────────────
        rotation += rotationSpeed * dt;
        arm.transform.setLocalRotation(
            glm::angleAxis(glm::radians(rotation), glm::vec3{0, 1, 0}));

        scriptRuntime.update(dt);
        physics.step(scene, dt);

        for (const auto &obj : scene.objects())
            if (obj->enabled && obj->animationPlayer)
                obj->animationPlayer->update(dt);

        // Skinning bone palette upload (must run after animation players).
        for (const auto &obj : scene.objects()) {
            if (!obj->enabled || !obj->skin || !obj->render) continue;
            const auto &skin = *obj->skin;
            for (int bi = 0; bi < skin.numBones; ++bi) {
                if (!skin.boneTransforms[bi]) continue;
                const glm::mat4 boneMatrix =
                    skin.boneTransforms[bi]->getModelMatrix()
                    * skin.inverseBindMatrices[bi];
                obj->render->material.set(
                    "uBoneMatrices[" + std::to_string(bi) + "]", boneMatrix);
            }
        }

        // ── Camera matrices ───────────────────────────────────────────────────
        const float aspect = fbSize.x > 0 && fbSize.y > 0
            ? static_cast<float>(fbSize.x) / static_cast<float>(fbSize.y)
            : 16.0f / 9.0f;
        const glm::mat4 viewMat = cameraObj.camera->viewMatrix(cameraObj.transform);
        const glm::mat4 projMat = cameraObj.camera->projectionMatrix(aspect);
        const glm::vec3 camPos  = cameraObj.transform.getWorldPosition();

        // Sync lamp emissive material color with its LightComponent.
        if (lamp.light) {
            lamp.render->material.set("uEmissiveColor",    lamp.light->color);
            lamp.render->material.set("uEmissiveStrength", lamp.light->intensity);
        }

        // ── Collect lights from scene objects ─────────────────────────────────
        sonnet::api::render::DirectionalLight ctxDirLight{};
        std::vector<sonnet::api::render::PointLight> ctxPointLights;
        for (const auto &obj : scene.objects()) {
            if (!obj->enabled || !obj->light || !obj->light->enabled) continue;
            using LT = sonnet::world::LightComponent::Type;
            if (obj->light->type == LT::Directional) {
                ctxDirLight = {obj->light->direction,
                               obj->light->color,
                               obj->light->intensity};
            } else {
                ctxPointLights.push_back({
                    .position  = obj->transform.getWorldPosition(),
                    .color     = obj->light->color,
                    .intensity = obj->light->intensity,
                });
                if (ctxPointLights.size() >= 8) break;
            }
        }

        sonnet::api::render::FrameContext ctx{
            .viewMatrix       = viewMat,
            .projectionMatrix = projMat,
            .viewPosition     = camPos,
            .viewportWidth    = static_cast<std::uint32_t>(fbSize.x),
            .viewportHeight   = static_cast<std::uint32_t>(fbSize.y),
            .deltaTime        = dt,
            .directionalLight = ctxDirLight,
            .pointLights      = ctxPointLights,
        };

        // ── Shadow maps (CSM + point-light cubemaps) ──────────────────────────
        const int shadowLightCount = shadows.render(
            scene, viewMat, projMat,
            cameraObj.camera->near,
            cameraObj.camera->fov,
            aspect, ctxPointLights);

        // ── Post-process (G-buffer → SSAO → deferred → sky → bloom → output) ─
        PostProcessParams ppParams{
            .viewMat          = viewMat,
            .projMat          = projMat,
            .fbSize           = {static_cast<int>(fbSize.x),
                                 static_cast<int>(fbSize.y)},
            .shadowLightCount = shadowLightCount,
            .shadowBias       = shadowBias,
            .pointShadowBias  = pointShadowBias,
            .exposure         = exposure,
            .bloomThreshold   = bloomThreshold,
            .bloomIntensity   = bloomIntensity,
            .bloomIterations  = bloomIterations,
            .ssaoEnabled      = ssaoEnabled,
            .ssaoShow         = ssaoShow,
            .ssaoRadius       = ssaoRadius,
            .ssaoBias         = ssaoBias,
            .fxaaEnabled      = fxaaEnabled,
            .outlineEnabled   = outlineEnabled,
            .outlineColor     = outlineColor,
            .ssrEnabled       = ssrEnabled,
            .ssrMaxSteps      = ssrMaxSteps,
            .ssrStepSize      = ssrStepSize,
            .ssrThickness     = ssrThickness,
            .ssrMaxDistance   = ssrMaxDistance,
            .ssrRoughnessMax  = ssrRoughnessMax,
            .ssrStrength      = ssrStrength,
            .scene            = &scene,
            .selectedObject   = ui.selectedObject(),
        };
        pp.execute(ppParams, ctx);

        // ── ImGui render ──────────────────────────────────────────────────────
        backend.bindDefaultRenderTarget();
        backend.setViewport(static_cast<std::uint32_t>(fbSize.x),
                            static_cast<std::uint32_t>(fbSize.y));
        backend.clear({.colors = {{0, {0.08f, 0.08f, 0.08f, 1.0f}}}});

        imgui.begin();

        EditorParams ep{
            .viewMat              = viewMat,
            .projMat              = projMat,
            .camPos               = camPos,
            .fbSize               = {static_cast<int>(fbSize.x),
                                     static_cast<int>(fbSize.y)},
            .viewportTexId        = viewportTexId,
            .ctx                  = &ctx,
            .pickingRT            = rts.pickingRT,
            .input                = &input,
            .cameraObj            = &cameraObj,
            .shaderReloadMsg      = shaderReloadMsg,
            .shaderReloadMsgTimer = shaderReloadMsgTimer,
            .shadowLightCount     = shadowLightCount,
            .csmSplitDepths       = &shadows.csmSplitDepths(),
            .rotationSpeed        = &rotationSpeed,
            .exposure             = &exposure,
            .shadowBias           = &shadowBias,
            .pointShadowBias      = &pointShadowBias,
            .bloomThreshold       = &bloomThreshold,
            .bloomIntensity       = &bloomIntensity,
            .bloomIterations      = &bloomIterations,
            .ssaoEnabled          = &ssaoEnabled,
            .ssaoRadius           = &ssaoRadius,
            .ssaoBias             = &ssaoBias,
            .ssaoShow             = &ssaoShow,
            .fxaaEnabled          = &fxaaEnabled,
            .outlineEnabled       = &outlineEnabled,
            .outlineColor         = &outlineColor,
            .ssrEnabled           = &ssrEnabled,
            .ssrMaxSteps          = &ssrMaxSteps,
            .ssrStrength          = &ssrStrength,
            .ssrStepSize          = &ssrStepSize,
            .ssrThickness         = &ssrThickness,
            .ssrMaxDistance       = &ssrMaxDistance,
            .ssrRoughnessMax      = &ssrRoughnessMax,
            .requestClose         = [&]{ window.requestClose(); },
        };
        ui.draw(ep);

        imgui.end();
        window.swapBuffers();
        input.nextFrame();
    }

    return 0;
}
