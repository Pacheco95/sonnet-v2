// Sonnet v2 — Phase 17 demo
// Shadow map → HDR offscreen pass → ACES tone-mapping → default framebuffer; ImGui debug panel (Tab).

#include <sonnet/api/render/Light.h>
#include <sonnet/input/InputSystem.h>
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

// ── Shadow-map pass shaders ───────────────────────────────────────────────────

static constexpr const char *SHADOW_VERT = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
}
)glsl";

static constexpr const char *SHADOW_FRAG = R"glsl(
#version 330 core
void main() {}
)glsl";

// ── Scene shaders (lit + shadow) ──────────────────────────────────────────────
// Vertex layout: position(0), texcoord(2), normal(3)

static constexpr const char *VERT_SRC = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uLightSpaceMatrix;

out vec3 vNormal;
out vec3 vFragPos;
out vec2 vTexCoord;
out vec4 vLightSpacePos;

void main() {
    vec4 worldPos    = uModel * vec4(aPosition, 1.0);
    gl_Position      = uProjection * uView * worldPos;
    vFragPos         = worldPos.xyz;
    vNormal          = mat3(transpose(inverse(uModel))) * aNormal;
    vTexCoord        = aTexCoord;
    vLightSpacePos   = uLightSpaceMatrix * worldPos;
}
)glsl";

static constexpr const char *FRAG_SRC = R"glsl(
#version 330 core
in  vec3 vNormal;
in  vec3 vFragPos;
in  vec2 vTexCoord;
in  vec4 vLightSpacePos;
out vec4 fragColor;

struct DirLight {
    vec3  direction;
    vec3  color;
    float intensity;
};
uniform DirLight  uDirLight;
uniform sampler2D uAlbedo;
uniform sampler2DShadow uShadowMap;
uniform float           uShadowBias;

// 3×3 PCF shadow factor using hardware depth comparison.
// Each texture() call on a sampler2DShadow returns a bilinearly filtered
// [0,1] result: 1.0 = lit, 0.0 = occluded.
float shadowFactor(vec3 n) {
    vec3 proj = vLightSpacePos.xyz / vLightSpacePos.w;
    proj = proj * 0.5 + 0.5;
    // Outside the shadow frustum → fully lit
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 ||
                        proj.y < 0.0 || proj.y > 1.0)
        return 1.0;
    float bias = max(uShadowBias * (1.0 - dot(n, normalize(uDirLight.direction))),
                     uShadowBias * 0.1);
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            shadow += texture(uShadowMap, vec3(proj.xy + vec2(x, y) * texelSize, proj.z - bias));
        }
    return shadow / 9.0;
}

void main() {
    vec3  n      = normalize(vNormal);
    float diff   = max(dot(n, normalize(uDirLight.direction)), 0.0);
    vec3  albedo = texture(uAlbedo, vTexCoord).rgb;
    float shadow = shadowFactor(n);
    vec3  col    = (0.15 + diff * uDirLight.intensity * shadow) * uDirLight.color * albedo;
    fragColor    = vec4(col, 1.0);
}
)glsl";

// ── Tone-mapping pass shaders ─────────────────────────────────────────────────

static constexpr const char *TONEMAP_VERT = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;
out vec2 vUV;
void main() {
    gl_Position = vec4(aPosition.xy, 0.0, 1.0);
    vUV = aTexCoord;
}
)glsl";

static constexpr const char *TONEMAP_FRAG = R"glsl(
#version 330 core
in  vec2 vUV;
out vec4 fragColor;
uniform sampler2D uHdrColor;
uniform float     uExposure;

// ACES filmic tone-mapping approximation (Krzysztof Narkowicz)
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr    = texture(uHdrColor, vUV).rgb * uExposure;
    fragColor   = vec4(aces(hdr), 1.0);
}
)glsl";

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

    // ── Lit shader and material template ─────────────────────────────────────
    const auto shaderHandle = renderer.createShader(VERT_SRC, FRAG_SRC);
    const auto matHandle    = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle  = shaderHandle,
        .renderState   = {},
        .defaultValues = {{"uShadowBias", 0.005f}},
    });

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
    const auto shadowShader  = renderer.createShader(SHADOW_VERT, SHADOW_FRAG);
    const auto shadowMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = shadowShader,
        .renderState  = {},
    });
    sonnet::api::render::MaterialInstance shadowMat{shadowMatTmpl};

    // ── Scene — loaded from JSON ───────────────────────────────────────────────
    sonnet::world::Scene scene;
    sonnet::scene::SceneLoader sceneLoader;
    sceneLoader.registerMaterial("lit",        matHandle);
    sceneLoader.registerTexture("shadowDepth", shadowDepthHandle);
    const auto objects = sceneLoader.load(
        DEMO_ASSETS_DIR "/scene.json",
        DEMO_ASSETS_DIR,
        scene,
        renderer);

    auto &arm       = *objects.at("Arm");
    auto &cube      = *objects.at("Cube");
    auto &floor     = *objects.at("Floor");
    auto &cameraObj = *objects.at("Camera");
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
    const auto tonemapShader  = renderer.createShader(TONEMAP_VERT, TONEMAP_FRAG);
    const auto tonemapMatTmpl = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = tonemapShader,
        .renderState  = {
            .depthTest = false,
            .depthWrite = false,
            .cull = sonnet::api::render::CullMode::None,
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
        floorMat.set("uShadowBias", shadowBias);

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

        // ── Pass 3: tone-map HDR → default framebuffer ────────────────────────
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

            if (ImGui::CollapsingHeader("Object", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("Rotation speed (°/s)", &rotationSpeed, 0.0f, 360.0f);
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
