#include "PostProcess.h"

#include <sonnet/api/render/IRenderTarget.h>
#include <sonnet/api/render/RenderItem.h>
#include <sonnet/world/GameObject.h>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <functional>
#include <string>
#include <vector>

using namespace sonnet::api::render;

static const RenderState kNoDepth{
    .depthTest  = false,
    .depthWrite = false,
    .cull       = CullMode::None,
};

PostProcess::PostProcess(sonnet::renderer::frontend::Renderer        &renderer,
                          sonnet::renderer::opengl::GlRendererBackend &backend,
                          ShaderRegistry                              &shaders,
                          const RenderTargets                         &rts,
                          const ShadowMaps                            &shadows,
                          const IBLMaps                               &ibl,
                          sonnet::core::GPUMeshHandle                  quadMesh,
                          sonnet::core::GPUMeshHandle                  sphereMesh,
                          sonnet::core::MaterialTemplateHandle         emissiveMatTmpl)
    : m_renderer(renderer), m_backend(backend),
      m_rts(rts), m_shadows(shadows),
      m_quadMesh(quadMesh), m_sphereMesh(sphereMesh),
      m_emissiveMatTmpl(emissiveMatTmpl)
{
    const float maxLOD = static_cast<float>(ibl.prefilteredLODs - 1);

    // ── Bloom bright-pass ─────────────────────────────────────────────────────
    const auto bloomBrightShader = shaders.compile(
        DEMO_ASSETS_DIR "/shaders/tonemap.vert",
        DEMO_ASSETS_DIR "/shaders/bloom_bright.frag");
    const auto bloomBrightTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = bloomBrightShader, .renderState = kNoDepth,
    });
    m_bloomBrightMat.emplace(bloomBrightTmpl);
    m_bloomBrightMat->addTexture("uHdrColor", rts.hdrTex);

    // ── Bloom blur ────────────────────────────────────────────────────────────
    const auto bloomBlurShader = shaders.compile(
        DEMO_ASSETS_DIR "/shaders/tonemap.vert",
        DEMO_ASSETS_DIR "/shaders/bloom_blur.frag");
    const auto bloomBlurTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = bloomBlurShader, .renderState = kNoDepth,
    });
    m_bloomBlurHMat.emplace(bloomBlurTmpl);
    m_bloomBlurVMat.emplace(bloomBlurTmpl);
    m_bloomBlurHMat->addTexture("uBloomTexture", rts.bloomBrightTex);
    m_bloomBlurVMat->addTexture("uBloomTexture", rts.bloomBlurTex);
    m_bloomBlurHMat->set("uHorizontal", 1);
    m_bloomBlurVMat->set("uHorizontal", 0);

    // ── SSR ───────────────────────────────────────────────────────────────────
    const auto ssrShader = shaders.compile(
        DEMO_ASSETS_DIR "/shaders/tonemap.vert",
        DEMO_ASSETS_DIR "/shaders/ssr.frag");
    const auto ssrTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = ssrShader, .renderState = kNoDepth,
    });
    m_ssrMat.emplace(ssrTmpl);
    m_ssrMat->addTexture("uDepth",           rts.gbufDepthTex);
    m_ssrMat->addTexture("uNormalMetallic",  rts.gbufNormalMetallicTex);
    m_ssrMat->addTexture("uAlbedoRoughness", rts.gbufAlbedoRoughTex);
    m_ssrMat->addTexture("uHDRColor",        rts.hdrTex);

    // ── Tone-map ──────────────────────────────────────────────────────────────
    const auto tonemapShader = shaders.compile(
        DEMO_ASSETS_DIR "/shaders/tonemap.vert",
        DEMO_ASSETS_DIR "/shaders/tonemap.frag");
    const auto tonemapTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = tonemapShader, .renderState = kNoDepth,
    });
    m_tonemapMat.emplace(tonemapTmpl);
    m_tonemapMat->addTexture("uHdrColor",     rts.hdrTex);
    m_tonemapMat->addTexture("uBloomTexture", rts.bloomBrightTex);
    m_tonemapMat->addTexture("uSSRTex",       rts.ssrTex);

    // ── FXAA ─────────────────────────────────────────────────────────────────
    const auto fxaaShader = shaders.compile(
        DEMO_ASSETS_DIR "/shaders/fxaa.vert",
        DEMO_ASSETS_DIR "/shaders/fxaa.frag");
    const auto fxaaTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = fxaaShader, .renderState = kNoDepth,
    });
    m_fxaaMat.emplace(fxaaTmpl);
    m_fxaaMat->addTexture("uScreen", rts.ldrTex);
    m_fxaaMat->addTexture("uDepth",  rts.gbufDepthTex);

    // ── SSAO ─────────────────────────────────────────────────────────────────
    const auto ssaoShader = shaders.compile(
        DEMO_ASSETS_DIR "/shaders/ssao.vert",
        DEMO_ASSETS_DIR "/shaders/ssao.frag");
    const auto ssaoTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = ssaoShader, .renderState = kNoDepth,
    });
    m_ssaoMat.emplace(ssaoTmpl);
    m_ssaoMat->addTexture("uNormalMap", rts.gbufNormalMetallicTex);
    m_ssaoMat->addTexture("uDepthMap",  rts.gbufDepthTex);
    m_ssaoMat->addTexture("uNoiseMap",  rts.ssaoNoiseTex);
    if (rts.ssaoKernel) {
        for (int i = 0; i < 64; ++i)
            m_ssaoMat->set("uKernel[" + std::to_string(i) + "]", (*rts.ssaoKernel)[i]);
    }

    // ── SSAO blur ─────────────────────────────────────────────────────────────
    const auto ssaoBlurShader = shaders.compile(
        DEMO_ASSETS_DIR "/shaders/ssao.vert",
        DEMO_ASSETS_DIR "/shaders/ssao_blur.frag");
    const auto ssaoBlurTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = ssaoBlurShader, .renderState = kNoDepth,
    });
    m_ssaoBlurMat.emplace(ssaoBlurTmpl);
    m_ssaoBlurMat->addTexture("uSSAOTexture", rts.ssaoTex);

    // ── SSAO debug (show raw AO as grayscale) ─────────────────────────────────
    const auto ssaoShowShader = shaders.compile(
        DEMO_ASSETS_DIR "/shaders/ssao.vert",
        DEMO_ASSETS_DIR "/shaders/ssao_show.frag");
    const auto ssaoShowTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = ssaoShowShader, .renderState = kNoDepth,
    });
    m_ssaoShowMat.emplace(ssaoShowTmpl);
    m_ssaoShowMat->addTexture("uSSAO", rts.ssaoBlurTex);

    // ── Outline mask (static mesh) ────────────────────────────────────────────
    const auto outlineMaskShader = shaders.compile(
        DEMO_ASSETS_DIR "/shaders/shadow.vert",
        DEMO_ASSETS_DIR "/shaders/outline_mask.frag");
    const auto outlineMaskTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = outlineMaskShader,
        .renderState  = { .depthTest = false, .depthWrite = false,
                          .cull = CullMode::None },
    });
    m_outlineMaskMat.emplace(outlineMaskTmpl);

    // ── Outline mask (skinned mesh) ───────────────────────────────────────────
    const auto outlineMaskSkinnedShader = shaders.compile(
        DEMO_ASSETS_DIR "/shaders/outline_mask_skinned.vert",
        DEMO_ASSETS_DIR "/shaders/outline_mask.frag");
    m_outlineMaskSkinnedMatTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = outlineMaskSkinnedShader,
        .renderState  = { .depthTest = false, .depthWrite = false,
                          .cull = CullMode::None },
    });

    // ── Outline composite ─────────────────────────────────────────────────────
    const auto outlineShader = shaders.compile(
        DEMO_ASSETS_DIR "/shaders/tonemap.vert",
        DEMO_ASSETS_DIR "/shaders/outline.frag");
    const auto outlineTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = outlineShader,
        .renderState  = { .depthTest = false, .depthWrite = false,
                          .blend = BlendMode::Alpha, .cull = CullMode::None },
    });
    m_outlineMat.emplace(outlineTmpl);
    m_outlineMat->addTexture("uMask", rts.outlineMaskTex);

    // ── Skybox ────────────────────────────────────────────────────────────────
    const auto skyShader = shaders.compile(
        DEMO_ASSETS_DIR "/shaders/sky.vert",
        DEMO_ASSETS_DIR "/shaders/sky.frag");
    const auto skyTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = skyShader,
        .renderState  = { .depthTest = false, .depthWrite = false,
                          .cull = CullMode::None },
    });
    m_skyMat.emplace(skyTmpl);
    m_skyMat->addTexture("uEnvMap", ibl.equirectHandle);
    m_skyMat->addTexture("gDepth",  rts.gbufDepthTex);

    // ── Deferred lighting ─────────────────────────────────────────────────────
    const auto deferredShader = shaders.compile(
        DEMO_ASSETS_DIR "/shaders/tonemap.vert",
        DEMO_ASSETS_DIR "/shaders/deferred_lighting.frag");
    const auto deferredTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = deferredShader, .renderState = kNoDepth,
    });
    m_deferredMat.emplace(deferredTmpl);
    m_deferredMat->addTexture("gAlbedoRoughness", rts.gbufAlbedoRoughTex);
    m_deferredMat->addTexture("gNormalMetallic",  rts.gbufNormalMetallicTex);
    m_deferredMat->addTexture("gEmissiveAO",      rts.gbufEmissiveAOTex);
    m_deferredMat->addTexture("gDepth",           rts.gbufDepthTex);
    for (int c = 0; c < ShadowMaps::NUM_CASCADES; ++c)
        m_deferredMat->addTexture("uShadowMaps[" + std::to_string(c) + "]",
                                  shadows.csmDepthHandles()[c]);
    m_deferredMat->addTexture("uIrradianceMap",  ibl.irradianceHandle);
    m_deferredMat->addTexture("uPrefilteredMap", ibl.prefilteredHandle);
    m_deferredMat->addTexture("uBRDFLUT",        ibl.brdfLUTHandle);
    m_deferredMat->addTexture("uSSAO",           rts.ssaoBlurTex);
    for (int i = 0; i < ShadowMaps::MAX_SHADOW_LIGHTS; ++i)
        m_deferredMat->addTexture("uPointShadowMaps[" + std::to_string(i) + "]",
                                  shadows.pointShadowHandles()[i]);
    m_deferredMat->set("uMaxPrefilteredLOD",   maxLOD);
    m_deferredMat->set("uPointShadowFarPlane", ShadowMaps::POINT_SHADOW_FAR);

    // ── Picking materials (public — used by EditorUI) ─────────────────────────
    const auto pickingShader = shaders.compile(
        DEMO_ASSETS_DIR "/shaders/shadow.vert",
        DEMO_ASSETS_DIR "/shaders/picking.frag");
    pickingMatTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = pickingShader,
        .renderState  = { .depthTest = true, .depthWrite = true,
                          .cull = CullMode::Back },
    });

    const auto pickingSkinnedShader = shaders.compile(
        DEMO_ASSETS_DIR "/shaders/outline_mask_skinned.vert",
        DEMO_ASSETS_DIR "/shaders/picking.frag");
    pickingSkinnedMatTmpl = renderer.createMaterial(MaterialTemplate{
        .shaderHandle = pickingSkinnedShader,
        .renderState  = { .depthTest = true, .depthWrite = true,
                          .cull = CullMode::Back },
    });
}

void PostProcess::fullscreenQuad(MaterialInstance &mat, const FrameContext &ppCtx) {
    const glm::mat4 identity{1.0f};
    std::vector<RenderItem> q{{
        .mesh        = m_quadMesh,
        .material    = mat,
        .modelMatrix = identity,
    }};
    m_renderer.beginFrame();
    m_renderer.render(ppCtx, q);
    m_renderer.endFrame();
}

void PostProcess::execute(const PostProcessParams &p, const FrameContext &ctx) {
    const glm::mat4 identity{1.0f};
    const FrameContext ppCtx{
        .viewMatrix       = identity,
        .projectionMatrix = identity,
        .viewPosition     = glm::vec3{0.0f},
        .viewportWidth    = static_cast<std::uint32_t>(p.fbSize.x),
        .viewportHeight   = static_cast<std::uint32_t>(p.fbSize.y),
        .deltaTime        = 0.0f,
    };

    // ── Pass 1.5: G-buffer ────────────────────────────────────────────────────
    m_renderer.bindRenderTarget(m_rts.gbufRT);
    m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                          static_cast<std::uint32_t>(p.fbSize.y));
    glDepthMask(GL_TRUE);
    m_backend.clear({
        .colors = {
            {0, {0.0f, 0.0f, 0.0f, 1.0f}},
            {1, {0.0f, 0.0f, 0.0f, 1.0f}},
            {2, {0.0f, 0.0f, 0.0f, 1.0f}},
        },
        .depth = 1.0f,
    });
    if (p.scene) {
        std::vector<RenderItem> gbufQueue;
        p.scene->buildRenderQueue(gbufQueue);

        // Emissive indicator spheres for point lights without a render component.
        for (const auto &obj : p.scene->objects()) {
            if (!obj->light || !obj->light->enabled) continue;
            if (obj->light->type != sonnet::world::LightComponent::Type::Point) continue;
            if (obj->render) continue;
            MaterialInstance indMat{m_emissiveMatTmpl};
            indMat.set("uEmissiveColor",    obj->light->color);
            indMat.set("uEmissiveStrength", obj->light->intensity);
            const glm::mat4 model =
                glm::translate(glm::mat4{1.0f}, obj->transform.getWorldPosition()) *
                glm::scale(glm::mat4{1.0f}, glm::vec3{0.08f});
            gbufQueue.push_back({
                .mesh        = m_sphereMesh,
                .material    = indMat,
                .modelMatrix = model,
            });
        }
        m_renderer.beginFrame();
        m_renderer.render(ctx, gbufQueue);
        m_renderer.endFrame();
    }

    // ── Pass 1.6 / 1.7: SSAO ─────────────────────────────────────────────────
    if (p.ssaoEnabled) {
        m_renderer.bindRenderTarget(m_rts.ssaoRT);
        m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                              static_cast<std::uint32_t>(p.fbSize.y));
        m_backend.clear({ .colors = {{0, {1.0f, 1.0f, 1.0f, 1.0f}}} });
        m_ssaoMat->set("uView",          p.viewMat);
        m_ssaoMat->set("uProjection",    p.projMat);
        m_ssaoMat->set("uInvProjection", p.invProjMat);
        m_ssaoMat->set("uNoiseScale",    glm::vec2{
            static_cast<float>(p.fbSize.x) / 4.0f,
            static_cast<float>(p.fbSize.y) / 4.0f});
        m_ssaoMat->set("uRadius", p.ssaoRadius);
        m_ssaoMat->set("uBias",   p.ssaoBias);
        fullscreenQuad(*m_ssaoMat, ppCtx);

        m_renderer.bindRenderTarget(m_rts.ssaoBlurRT);
        m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                              static_cast<std::uint32_t>(p.fbSize.y));
        m_backend.clear({ .colors = {{0, {1.0f, 1.0f, 1.0f, 1.0f}}} });
        fullscreenQuad(*m_ssaoBlurMat, ppCtx);
    } else {
        m_renderer.bindRenderTarget(m_rts.ssaoBlurRT);
        m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                              static_cast<std::uint32_t>(p.fbSize.y));
        m_backend.clear({ .colors = {{0, {1.0f, 1.0f, 1.0f, 1.0f}}} });
    }

    // ── Pass 2: Deferred lighting ─────────────────────────────────────────────
    m_renderer.bindRenderTarget(m_rts.hdrRT);
    m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                          static_cast<std::uint32_t>(p.fbSize.y));
    m_backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
    {
        const glm::mat4 invViewProj = glm::inverse(p.projMat * p.viewMat);
        m_deferredMat->set("uInvViewProj", invViewProj);
        m_deferredMat->set("uShadowBias",  p.shadowBias);
        m_deferredMat->set("uPointShadowCount", p.shadowLightCount);
        m_deferredMat->set("uPointShadowBias",  p.pointShadowBias);
        for (int c = 0; c < ShadowMaps::NUM_CASCADES; ++c) {
            m_deferredMat->set("uCSMLightSpaceMats[" + std::to_string(c) + "]",
                               m_shadows.csmLightSpaceMats()[c]);
            m_deferredMat->set("uCSMSplitDepths["   + std::to_string(c) + "]",
                               m_shadows.csmSplitDepths()[c]);
        }
        std::vector<RenderItem> dq{{
            .mesh        = m_quadMesh,
            .material    = *m_deferredMat,
            .modelMatrix = identity,
        }};
        m_renderer.beginFrame();
        m_renderer.render(ctx, dq);
        m_renderer.endFrame();
    }

    // ── Pass 2.1: Sky ─────────────────────────────────────────────────────────
    {
        std::vector<RenderItem> skyQ{{
            .mesh = m_quadMesh, .material = *m_skyMat, .modelMatrix = identity,
        }};
        m_renderer.beginFrame();
        m_renderer.render(ctx, skyQ);
        m_renderer.endFrame();
    }

    // ── Pass 2.15: Selection outline mask ─────────────────────────────────────
    if (p.outlineEnabled && p.selectedObject && p.scene) {
        std::vector<RenderItem> outlineQueue;
        std::function<void(const sonnet::world::GameObject &)> collectSubtree =
            [&](const sonnet::world::GameObject &obj) {
                if (obj.render) {
                    if (obj.skin) {
                        MaterialInstance skinnedMat{m_outlineMaskSkinnedMatTmpl};
                        for (const auto &[name, val] : obj.render->material.values())
                            if (name.rfind("uBoneMatrices", 0) == 0)
                                skinnedMat.set(name, val);
                        outlineQueue.push_back({
                            .mesh        = obj.render->mesh,
                            .material    = skinnedMat,
                            .modelMatrix = obj.transform.getModelMatrix(),
                        });
                    } else {
                        outlineQueue.push_back({
                            .mesh        = obj.render->mesh,
                            .material    = *m_outlineMaskMat,
                            .modelMatrix = obj.transform.getModelMatrix(),
                        });
                    }
                }
                for (auto *childTf : obj.transform.children()) {
                    for (const auto &o : p.scene->objects())
                        if (&o->transform == childTf) { collectSubtree(*o); break; }
                }
            };
        collectSubtree(*p.selectedObject);

        m_renderer.bindRenderTarget(m_rts.outlineMaskRT);
        m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                              static_cast<std::uint32_t>(p.fbSize.y));
        m_backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
        m_renderer.beginFrame();
        m_renderer.render(ctx, outlineQueue);
        m_renderer.endFrame();

        m_renderer.bindRenderTarget(m_rts.hdrRT);
        m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                              static_cast<std::uint32_t>(p.fbSize.y));
        m_outlineMat->set("uOutlineColor", p.outlineColor);
        fullscreenQuad(*m_outlineMat, ppCtx);
    }

    // ── Pass 2.2: SSR ─────────────────────────────────────────────────────────
    m_renderer.bindRenderTarget(m_rts.ssrRT);
    m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                          static_cast<std::uint32_t>(p.fbSize.y));
    m_backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
    if (p.ssrEnabled) {
        m_ssrMat->set("uProjection",    p.projMat);
        m_ssrMat->set("uInvProjection", p.invProjMat);
        m_ssrMat->set("uView",          p.viewMat);
        m_ssrMat->set("uResolution",    glm::vec2(static_cast<float>(p.fbSize.x),
                                                   static_cast<float>(p.fbSize.y)));
        m_ssrMat->set("uMaxSteps",     p.ssrMaxSteps);
        m_ssrMat->set("uStepSize",     p.ssrStepSize);
        m_ssrMat->set("uThickness",    p.ssrThickness);
        m_ssrMat->set("uMaxDistance",  p.ssrMaxDistance);
        m_ssrMat->set("uRoughnessMax", p.ssrRoughnessMax);
        fullscreenQuad(*m_ssrMat, ppCtx);
    }

    // ── Pass 2.5: Bloom ───────────────────────────────────────────────────────
    m_renderer.bindRenderTarget(m_rts.bloomBrightRT);
    m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                          static_cast<std::uint32_t>(p.fbSize.y));
    m_backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
    m_bloomBrightMat->set("uBloomThreshold", p.bloomThreshold);
    fullscreenQuad(*m_bloomBrightMat, ppCtx);

    for (int i = 0; i < p.bloomIterations; ++i) {
        m_renderer.bindRenderTarget(m_rts.bloomBlurRT);
        m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                              static_cast<std::uint32_t>(p.fbSize.y));
        m_backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
        fullscreenQuad(*m_bloomBlurHMat, ppCtx);

        m_renderer.bindRenderTarget(m_rts.bloomBrightRT);
        m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                              static_cast<std::uint32_t>(p.fbSize.y));
        m_backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
        fullscreenQuad(*m_bloomBlurVMat, ppCtx);
    }

    m_tonemapMat->set("uExposure",       p.exposure);
    m_tonemapMat->set("uBloomIntensity", p.bloomIntensity);
    m_tonemapMat->set("uSSRStrength",    p.ssrEnabled ? p.ssrStrength : 0.0f);

    // ── Pass 3 / 4: Tone-map + optional FXAA → viewportRT ────────────────────
    if (p.ssaoShow) {
        m_renderer.bindRenderTarget(m_rts.viewportRT);
        m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                              static_cast<std::uint32_t>(p.fbSize.y));
        m_backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
        fullscreenQuad(*m_ssaoShowMat, ppCtx);
    } else if (p.fxaaEnabled) {
        m_renderer.bindRenderTarget(m_rts.ldrRT);
        m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                              static_cast<std::uint32_t>(p.fbSize.y));
        m_backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
        fullscreenQuad(*m_tonemapMat, ppCtx);

        m_renderer.bindRenderTarget(m_rts.viewportRT);
        m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                              static_cast<std::uint32_t>(p.fbSize.y));
        m_backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
        m_fxaaMat->set("uTexelSize", glm::vec2(1.0f / static_cast<float>(p.fbSize.x),
                                               1.0f / static_cast<float>(p.fbSize.y)));
        fullscreenQuad(*m_fxaaMat, ppCtx);
    } else {
        m_renderer.bindRenderTarget(m_rts.viewportRT);
        m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                              static_cast<std::uint32_t>(p.fbSize.y));
        m_backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}} });
        fullscreenQuad(*m_tonemapMat, ppCtx);
    }
}
