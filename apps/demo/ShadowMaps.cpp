#include "ShadowMaps.h"

#include "IBL.h" // for RawGLCubeMap

#include <sonnet/api/render/IRenderTarget.h>
#include <sonnet/core/RendererTraits.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

using namespace sonnet::api::render;

ShadowMaps::ShadowMaps(sonnet::renderer::frontend::Renderer        &renderer,
                        sonnet::renderer::opengl::GlRendererBackend &backend,
                        sonnet::core::ShaderHandle shadowShader,
                        sonnet::core::ShaderHandle ptShadowShader)
    : m_renderer(renderer), m_backend(backend)
{
    // ── CSM render targets (NUM_CASCADES × Depth24, SHADOW_SIZE²) ────────────
    const SamplerDesc csmSamplerDesc{
        .minFilter    = MinFilter::Linear,
        .magFilter    = MagFilter::Linear,
        .wrapS        = TextureWrap::ClampToEdge,
        .wrapT        = TextureWrap::ClampToEdge,
        .depthCompare = true,
    };
    for (int i = 0; i < NUM_CASCADES; ++i) {
        m_csmRTHandles[i] = renderer.createRenderTarget(RenderTargetDesc{
            .width  = static_cast<std::uint32_t>(SHADOW_SIZE),
            .height = static_cast<std::uint32_t>(SHADOW_SIZE),
            .colors = {},
            .depth  = TextureAttachmentDesc{
                .format      = TextureFormat::Depth24,
                .samplerDesc = csmSamplerDesc,
            },
        });
        m_csmDepthHandles[i] = renderer.depthTextureHandle(m_csmRTHandles[i]);
    }

    // ── Point-light shadow cubemaps (MAX_SHADOW_LIGHTS × R32F, POINT_SHADOW_SIZE²) ─
    glGenTextures(MAX_SHADOW_LIGHTS, m_pointShadowCubeTex.data());
    for (int i = 0; i < MAX_SHADOW_LIGHTS; ++i) {
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_pointShadowCubeTex[i]);
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

    glGenFramebuffers(1, &m_pointShadowFBO);
    glGenRenderbuffers(1, &m_pointShadowRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_pointShadowRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          POINT_SHADOW_SIZE, POINT_SHADOW_SIZE);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, m_pointShadowFBO);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, m_pointShadowRBO);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Register cubemaps with the engine renderer.
    for (int i = 0; i < MAX_SHADOW_LIGHTS; ++i)
        m_pointShadowHandles[i] = renderer.registerRawTexture(
            std::make_unique<RawGLCubeMap>(m_pointShadowCubeTex[i]));

    // ── Shadow materials ──────────────────────────────────────────────────────
    m_shadowMatTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = shadowShader,
        .renderState  = {},
    });
    m_shadowMat.emplace(m_shadowMatTmpl);

    m_ptShadowMatTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = ptShadowShader,
        .renderState  = {},
    });
    m_ptShadowMat.emplace(m_ptShadowMatTmpl);
}

int ShadowMaps::render(const sonnet::world::Scene                         &scene,
                        const glm::mat4                                    &viewMat,
                        const glm::mat4                                    &projMat,
                        float camNear, float camFov, float aspect,
                        const std::vector<sonnet::api::render::PointLight> &pointLights) {
    // ── Cascade split depths (blend of log and uniform) ───────────────────────
    constexpr float csmFar    = 50.0f;
    constexpr float csmLambda = 0.75f;

    for (int i = 0; i < NUM_CASCADES; ++i) {
        const float p        = static_cast<float>(i + 1) / static_cast<float>(NUM_CASCADES);
        const float logSplit = camNear * std::pow(csmFar / camNear, p);
        const float uniSplit = camNear + (csmFar - camNear) * p;
        m_csmSplitDepths[i]  = csmLambda * logSplit + (1.0f - csmLambda) * uniSplit;
    }

    // ── Directional light view matrix ─────────────────────────────────────────
    glm::vec3 lightDir{0.6f, 1.0f, 0.4f};
    for (const auto &obj : scene.objects()) {
        if (obj->light && obj->light->type == sonnet::world::LightComponent::Type::Directional) {
            lightDir = obj->light->direction;
            break;
        }
    }
    const glm::vec3 lightDirNorm = glm::normalize(lightDir);
    const glm::vec3 lightUp = std::abs(lightDirNorm.y) > 0.99f
                            ? glm::vec3{0.0f, 0.0f, 1.0f}
                            : glm::vec3{0.0f, 1.0f, 0.0f};
    const glm::mat4 lightView = glm::lookAt(-lightDirNorm, glm::vec3{0.0f}, lightUp);

    const glm::vec4 ndcCorners[8] = {
        {-1,-1,-1,1},{1,-1,-1,1},{-1,1,-1,1},{1,1,-1,1},
        {-1,-1, 1,1},{1,-1, 1,1},{-1,1, 1,1},{1,1, 1,1},
    };

    // Shadow geometry queue (shared for all cascades).
    std::vector<RenderItem> shadowQueue;
    for (const auto &obj : scene.objects()) {
        if (!obj->enabled || !obj->render) continue;
        shadowQueue.push_back({
            .mesh        = obj->render->mesh,
            .material    = *m_shadowMat,
            .modelMatrix = obj->transform.getModelMatrix(),
        });
    }

    // ── Per-cascade render ────────────────────────────────────────────────────
    float prevSplit = camNear;
    for (int c = 0; c < NUM_CASCADES; ++c) {
        const float splitDepth = m_csmSplitDepths[c];

        const glm::mat4 cascadeProj = sonnet::core::projection::perspective(
            glm::radians(camFov), aspect, prevSplit, splitDepth);
        const glm::mat4 invCamVP = glm::inverse(cascadeProj * viewMat);

        float minX =  1e9f, maxX = -1e9f;
        float minY =  1e9f, maxY = -1e9f;
        for (const auto &nc : ndcCorners) {
            glm::vec4 world = invCamVP * nc;
            world /= world.w;
            glm::vec4 ls = lightView * world;
            minX = std::min(minX, ls.x); maxX = std::max(maxX, ls.x);
            minY = std::min(minY, ls.y); maxY = std::max(maxY, ls.y);
        }
        const glm::mat4 cascadeOrtho = sonnet::core::projection::ortho(
            minX, maxX, minY, maxY, -100.0f, 100.0f);
        m_csmLightSpaceMats[c] = cascadeOrtho * lightView;

        m_renderer.bindRenderTarget(m_csmRTHandles[c]);
        m_backend.setViewport(static_cast<std::uint32_t>(SHADOW_SIZE),
                              static_cast<std::uint32_t>(SHADOW_SIZE));
        glDepthMask(GL_TRUE);
        m_backend.clear({ .depth = 1.0f });

        FrameContext shadowCtx{
            .viewMatrix       = lightView,
            .projectionMatrix = cascadeOrtho,
            .viewPosition     = glm::vec3{0.0f},
            .viewportWidth    = static_cast<std::uint32_t>(SHADOW_SIZE),
            .viewportHeight   = static_cast<std::uint32_t>(SHADOW_SIZE),
            .deltaTime        = 0.0f,
        };
        m_renderer.beginFrame();
        m_renderer.render(shadowCtx, shadowQueue);
        m_renderer.endFrame();

        prevSplit = splitDepth;
    }

    // ── Point-light shadow cubemaps ───────────────────────────────────────────
    const glm::mat4 ptShadowProj = sonnet::core::projection::perspective(
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

    std::vector<RenderItem> ptShadowQueue;
    for (const auto &obj : scene.objects()) {
        if (!obj->enabled || !obj->render) continue;
        ptShadowQueue.push_back({
            .mesh        = obj->render->mesh,
            .material    = *m_ptShadowMat,
            .modelMatrix = obj->transform.getModelMatrix(),
        });
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_pointShadowFBO);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    m_backend.setViewport(static_cast<std::uint32_t>(POINT_SHADOW_SIZE),
                          static_cast<std::uint32_t>(POINT_SHADOW_SIZE));

    int shadowLightCount = 0;
    for (const auto &pl : pointLights) {
        if (shadowLightCount >= MAX_SHADOW_LIGHTS) break;
        const int  shadowIdx = shadowLightCount;
        const auto faceViews = faceViewsFor(pl.position);
        m_ptShadowMat->set("uLightPos", pl.position);
        m_ptShadowMat->set("uFarPlane", POINT_SHADOW_FAR);

        for (int f = 0; f < 6; ++f) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + f,
                                   m_pointShadowCubeTex[shadowIdx], 0);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            FrameContext faceCtx{
                .viewMatrix       = faceViews[f],
                .projectionMatrix = ptShadowProj,
                .viewPosition     = pl.position,
                .viewportWidth    = static_cast<std::uint32_t>(POINT_SHADOW_SIZE),
                .viewportHeight   = static_cast<std::uint32_t>(POINT_SHADOW_SIZE),
                .deltaTime        = 0.0f,
            };
            m_renderer.beginFrame();
            m_renderer.render(faceCtx, ptShadowQueue);
            m_renderer.endFrame();
        }
        ++shadowLightCount;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    return shadowLightCount;
}
