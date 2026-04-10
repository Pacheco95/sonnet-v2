// Sonnet v2 — Phase 22 demo
// Shadow map -> HDR offscreen pass -> ACES tone-mapping -> default framebuffer; ImGui debug panel (Tab).

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
    auto &cubeMat   = cube.render->material;
    auto &floorMat  = floor.render->material;

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
    sonnet::api::render::MaterialInstance tonemapMat{tonemapMatTmpl};
    tonemapMat.addTexture("uHdrColor", hdrTexHandle);

    FlyCamera flyCamera{cameraObj.transform};

    // Tweakable state exposed via ImGui.
    float     rotationSpeed  = 45.0f;
    glm::vec3 lightDir       = {0.6f, 1.0f, 0.4f};
    glm::vec3 lightColor     = {1.0f, 1.0f, 1.0f};
    float     lightIntensity = 1.0f;
    float     exposure       = 1.0f;
    float     shadowBias     = 0.005f;
    bool      uiMode         = false;

    // Per-object PBR scalar multipliers — applied on top of the ORM texture.
    // 1.0 = let the texture drive everything.
    float cubeMetallic   = 1.0f;
    float cubeRoughness  = 1.0f;
    float floorMetallic  = 1.0f;
    float floorRoughness = 1.0f;

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

        const float aspect = fbSize.x > 0 && fbSize.y > 0
            ? static_cast<float>(fbSize.x) / static_cast<float>(fbSize.y)
            : 16.0f / 9.0f;
        const glm::mat4 viewMat = cameraObj.camera->viewMatrix(cameraObj.transform);
        const glm::mat4 projMat = cameraObj.camera->projectionMatrix(aspect);
        const glm::vec3 camPos  = cameraObj.transform.getWorldPosition();

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

        renderer.beginFrame();
        renderer.render(ctx, queue);
        renderer.endFrame();

        // ── Pass 3: tone-map HDR -> default framebuffer ───────────────────────
        backend.bindDefaultRenderTarget();
        backend.setViewport(fbSize.x, fbSize.y);
        backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });

        tonemapMat.set("uExposure", exposure);

        const glm::mat4 identity{1.0f};
        const glm::vec3 origin{0.0f};
        sonnet::api::render::FrameContext tonemapCtx{
            .viewMatrix       = identity,
            .projectionMatrix = identity,
            .viewPosition     = origin,
            .viewportWidth    = fbSize.x,
            .viewportHeight   = fbSize.y,
            .deltaTime        = 0.0f,
        };
        std::vector<sonnet::api::render::RenderItem> tonemapQueue{{
            .mesh        = quadMeshHandle,
            .material    = tonemapMat,
            .modelMatrix = glm::mat4{1.0f},
        }};
        renderer.beginFrame();
        renderer.render(tonemapCtx, tonemapQueue);
        renderer.endFrame();

        // ── ImGui ──────────────────────────────────────────────────────────────
        imgui.begin();
        if (uiMode) {
            ImGui::Begin("Debug  [Tab to close]");

            if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("Direction",  &lightDir.x,   0.01f, -1.0f, 1.0f);
                ImGui::ColorEdit3("Color",      &lightColor.x);
                ImGui::SliderFloat("Intensity", &lightIntensity, 0.0f, 4.0f);
            }

            if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("Bias", &shadowBias, 0.0001f, 0.05f, "%.4f");
            }

            if (ImGui::CollapsingHeader("Tone-mapping", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("Exposure", &exposure, 0.1f, 5.0f);
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
