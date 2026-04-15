#include "PostProcess.h"

#include <sonnet/api/render/IRenderTarget.h>
#include <sonnet/api/render/IRendererBackend.h>
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
      m_emissiveMatTmpl(emissiveMatTmpl),
      m_graph(renderer, backend)
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

    // ── SSAO debug ────────────────────────────────────────────────────────────
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

    // ── Sky ───────────────────────────────────────────────────────────────────
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

    // ── Picking (public — used by EditorUI) ───────────────────────────────────
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

// ── Private helpers ───────────────────────────────────────────────────────────

void PostProcess::fullscreenQuad(MaterialInstance &mat, const FrameContext &ppCtx)
{
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

// ── buildGraph ────────────────────────────────────────────────────────────────
// Registers all passes into m_graph using the current toggle state in m_params.
// Sky and outline composite are folded into the "Deferred" pass so that hdrRT
// has exactly one declared writer, keeping downstream read dependencies clean.
void PostProcess::buildGraph()
{
    m_graph.reset();

    // Register the tex→RT source mapping so the graph can resolve reads.
    m_graph.registerTexSource(m_rts.gbufRT,        m_rts.gbufAlbedoRoughTex);
    m_graph.registerTexSource(m_rts.gbufRT,        m_rts.gbufNormalMetallicTex);
    m_graph.registerTexSource(m_rts.gbufRT,        m_rts.gbufEmissiveAOTex);
    m_graph.registerTexSource(m_rts.gbufRT,        m_rts.gbufDepthTex);
    m_graph.registerTexSource(m_rts.hdrRT,         m_rts.hdrTex);
    m_graph.registerTexSource(m_rts.ssaoRT,        m_rts.ssaoTex);
    m_graph.registerTexSource(m_rts.ssaoBlurRT,    m_rts.ssaoBlurTex);
    m_graph.registerTexSource(m_rts.bloomBrightRT, m_rts.bloomBrightTex);
    m_graph.registerTexSource(m_rts.bloomBlurRT,   m_rts.bloomBlurTex);
    m_graph.registerTexSource(m_rts.ssrRT,         m_rts.ssrTex);
    m_graph.registerTexSource(m_rts.outlineMaskRT, m_rts.outlineMaskTex);
    m_graph.registerTexSource(m_rts.ldrRT,         m_rts.ldrTex);
    m_graph.registerTexSource(m_rts.viewportRT,    m_rts.viewportTex);

    // ── GBuffer ───────────────────────────────────────────────────────────────
    m_graph.addPass("GBuffer",
        /*reads=*/ {},
        m_rts.gbufRT,
        RGClearDesc{
            .colors = {{0,{0,0,0,1}},{1,{0,0,0,1}},{2,{0,0,0,1}}},
            .depth  = 1.0f,
        },
        /*isOutput=*/ false,
        [this](const FrameContext &ctx, const FrameContext &) {
            if (!m_params.scene) return;
            std::vector<RenderItem> queue;
            m_params.scene->buildRenderQueue(queue);
            // Emissive indicator spheres for point lights without a render mesh.
            for (const auto &obj : m_params.scene->objects()) {
                if (!obj->light || !obj->light->enabled) continue;
                if (obj->light->type != sonnet::world::LightComponent::Type::Point) continue;
                if (obj->render) continue;
                MaterialInstance indMat{m_emissiveMatTmpl};
                indMat.set("uEmissiveColor",    obj->light->color);
                indMat.set("uEmissiveStrength", obj->light->intensity);
                const glm::mat4 model =
                    glm::translate(glm::mat4{1.0f}, obj->transform.getWorldPosition()) *
                    glm::scale(glm::mat4{1.0f}, glm::vec3{0.08f});
                queue.push_back({
                    .mesh        = m_sphereMesh,
                    .material    = indMat,
                    .modelMatrix = model,
                });
            }
            m_renderer.beginFrame();
            m_renderer.render(ctx, queue);
            m_renderer.endFrame();
        });

    // ── SSAO (or white fill when disabled) ────────────────────────────────────
    if (m_params.ssaoEnabled) {
        m_graph.addPass("SSAO",
            {m_rts.gbufNormalMetallicTex, m_rts.gbufDepthTex},
            m_rts.ssaoRT,
            RGClearDesc{.colors = {{0,{1,1,1,1}}}},
            false,
            [this](const FrameContext &, const FrameContext &ppCtx) {
                fullscreenQuad(*m_ssaoMat, ppCtx);
            });

        m_graph.addPass("SSAOBlur",
            {m_rts.ssaoTex},
            m_rts.ssaoBlurRT,
            RGClearDesc{.colors = {{0,{1,1,1,1}}}},
            false,
            [this](const FrameContext &, const FrameContext &ppCtx) {
                fullscreenQuad(*m_ssaoBlurMat, ppCtx);
            });
    } else {
        m_graph.addPass("SSAODisabled",
            {},
            m_rts.ssaoBlurRT,
            RGClearDesc{.colors = {{0,{1,1,1,1}}}},
            false,
            [](const FrameContext &, const FrameContext &) {});
    }

    // ── Deferred lighting + Sky (and inline outline when enabled) ─────────────
    // Sky and outline composite are folded here so hdrRT has exactly one
    // declared writer — downstream passes (SSR, BloomBright) always depend
    // on a single Deferred node, regardless of which overlays are active.
    m_graph.addPass("Deferred",
        {m_rts.gbufAlbedoRoughTex, m_rts.gbufNormalMetallicTex,
         m_rts.gbufEmissiveAOTex,  m_rts.gbufDepthTex, m_rts.ssaoBlurTex},
        m_rts.hdrRT,
        RGClearDesc{.colors = {{0,{0,0,0,1}}}},
        false,
        [this](const FrameContext &ctx, const FrameContext &ppCtx) {
            const glm::mat4 identity{1.0f};

            // Deferred lighting quad
            std::vector<RenderItem> dq{{
                .mesh = m_quadMesh, .material = *m_deferredMat, .modelMatrix = identity,
            }};
            m_renderer.beginFrame();
            m_renderer.render(ctx, dq);
            m_renderer.endFrame();

            // Sky (reads gDepth; discards fragments where geometry depth < 1.0)
            std::vector<RenderItem> skyQ{{
                .mesh = m_quadMesh, .material = *m_skyMat, .modelMatrix = identity,
            }};
            m_renderer.beginFrame();
            m_renderer.render(ctx, skyQ);
            m_renderer.endFrame();

            // Outline mask + composite (when a selection is active)
            if (m_params.outlineEnabled && m_params.selectedObject && m_params.scene) {
                // Build outline geometry queue for selected object subtree.
                std::vector<RenderItem> outlineQueue;
                std::function<void(const sonnet::world::GameObject &)> collect =
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
                        for (auto *tf : obj.transform.children())
                            for (const auto &o : m_params.scene->objects())
                                if (&o->transform == tf) { collect(*o); break; }
                    };
                collect(*m_params.selectedObject);

                // Render mask to outlineMaskRT
                m_renderer.bindRenderTarget(m_rts.outlineMaskRT);
                m_backend.setViewport(
                    static_cast<std::uint32_t>(m_params.fbSize.x),
                    static_cast<std::uint32_t>(m_params.fbSize.y));
                m_backend.clear({.colors = {{0u, glm::vec4{0,0,0,1}}}});
                m_renderer.beginFrame();
                m_renderer.render(ctx, outlineQueue);
                m_renderer.endFrame();

                // Composite outline edge onto hdrRT (alpha blend)
                m_renderer.bindRenderTarget(m_rts.hdrRT);
                m_backend.setViewport(
                    static_cast<std::uint32_t>(m_params.fbSize.x),
                    static_cast<std::uint32_t>(m_params.fbSize.y));
                m_outlineMat->set("uOutlineColor", m_params.outlineColor);
                fullscreenQuad(*m_outlineMat, ppCtx);
            }
        });

    // ── SSR (or black fill when disabled) ─────────────────────────────────────
    if (m_params.ssrEnabled) {
        m_graph.addPass("SSR",
            {m_rts.gbufDepthTex, m_rts.gbufNormalMetallicTex,
             m_rts.gbufAlbedoRoughTex, m_rts.hdrTex},
            m_rts.ssrRT,
            RGClearDesc{.colors = {{0,{0,0,0,1}}}},
            false,
            [this](const FrameContext &, const FrameContext &ppCtx) {
                fullscreenQuad(*m_ssrMat, ppCtx);
            });
    } else {
        m_graph.addPass("SSRDisabled",
            {},
            m_rts.ssrRT,
            RGClearDesc{.colors = {{0,{0,0,0,1}}}},
            false,
            [](const FrameContext &, const FrameContext &) {});
    }

    // ── Bloom bright-pass ─────────────────────────────────────────────────────
    m_graph.addPass("BloomBright",
        {m_rts.hdrTex},
        m_rts.bloomBrightRT,
        RGClearDesc{.colors = {{0,{0,0,0,1}}}},
        false,
        [this](const FrameContext &, const FrameContext &ppCtx) {
            fullscreenQuad(*m_bloomBrightMat, ppCtx);
        });

    // ── Bloom blur (ping-pong; final result ends up in bloomBrightRT) ──────────
    // Declares writesRT=bloomBlurRT (first writer) so downstream passes that
    // read bloomBlurTex depend on this node and are scheduled after it.
    // Tonemap additionally reads bloomBlurTex to enforce ordering after this pass.
    m_graph.addPass("BloomBlur",
        {m_rts.bloomBrightTex},
        m_rts.bloomBlurRT,
        RGClearDesc{.colors = {{0,{0,0,0,1}}}},
        false,
        [this](const FrameContext &, const FrameContext &ppCtx) {
            // Graph has already bound and cleared bloomBlurRT for the first H pass.
            for (int i = 0; i < m_params.bloomIterations; ++i) {
                if (i > 0) {
                    // Re-bind for subsequent H passes.
                    m_renderer.bindRenderTarget(m_rts.bloomBlurRT);
                    m_backend.setViewport(ppCtx.viewportWidth, ppCtx.viewportHeight);
                    m_backend.clear({.colors = {{0u, glm::vec4{0,0,0,1}}}});
                }
                fullscreenQuad(*m_bloomBlurHMat, ppCtx); // H: bloomBright → bloomBlur

                m_renderer.bindRenderTarget(m_rts.bloomBrightRT);
                m_backend.setViewport(ppCtx.viewportWidth, ppCtx.viewportHeight);
                m_backend.clear({.colors = {{0u, glm::vec4{0,0,0,1}}}});
                fullscreenQuad(*m_bloomBlurVMat, ppCtx); // V: bloomBlur  → bloomBright
            }
            // Final blurred bloom is now in bloomBrightRT.
        });

    // ── Final output: SSAOShow / Tonemap+FXAA / Tonemap alone ─────────────────
    if (m_params.ssaoShow) {
        // Debug view: show raw AO buffer. All other post-process passes are culled.
        m_graph.addPass("SSAOShow",
            {m_rts.ssaoBlurTex},
            m_rts.viewportRT,
            RGClearDesc{.colors = {{0,{0,0,0,1}}}},
            /*isOutput=*/ true,
            [this](const FrameContext &, const FrameContext &ppCtx) {
                fullscreenQuad(*m_ssaoShowMat, ppCtx);
            });
    } else if (m_params.fxaaEnabled) {
        m_graph.addPass("Tonemap",
            // Extra read of bloomBlurTex forces ordering after BloomBlur
            // (final bloom is in bloomBrightRT but BloomBlur declared bloomBlurRT).
            {m_rts.hdrTex, m_rts.bloomBrightTex, m_rts.ssrTex, m_rts.bloomBlurTex},
            m_rts.ldrRT,
            RGClearDesc{.colors = {{0,{0,0,0,1}}}},
            false,
            [this](const FrameContext &, const FrameContext &ppCtx) {
                fullscreenQuad(*m_tonemapMat, ppCtx);
            });

        m_graph.addPass("FXAA",
            {m_rts.ldrTex},
            m_rts.viewportRT,
            RGClearDesc{.colors = {{0,{0,0,0,1}}}},
            /*isOutput=*/ true,
            [this](const FrameContext &, const FrameContext &ppCtx) {
                fullscreenQuad(*m_fxaaMat, ppCtx);
            });
    } else {
        m_graph.addPass("Tonemap",
            {m_rts.hdrTex, m_rts.bloomBrightTex, m_rts.ssrTex, m_rts.bloomBlurTex},
            m_rts.viewportRT,
            RGClearDesc{.colors = {{0,{0,0,0,1}}}},
            /*isOutput=*/ true,
            [this](const FrameContext &, const FrameContext &ppCtx) {
                fullscreenQuad(*m_tonemapMat, ppCtx);
            });
    }

    m_graph.compile();
}

// ── execute ───────────────────────────────────────────────────────────────────

void PostProcess::execute(const PostProcessParams &p, const FrameContext &ctx)
{
    // Store current-frame params; pass callbacks read from m_params.
    m_params = p;

    // Rebuild the graph when any structural toggle changes.
    const bool hasSelection = p.outlineEnabled && p.selectedObject != nullptr;
    if (!m_graphBuilt         ||
        p.ssaoEnabled != m_cachedSsaoEnabled ||
        p.ssaoShow    != m_cachedSsaoShow    ||
        p.fxaaEnabled != m_cachedFxaaEnabled ||
        p.ssrEnabled  != m_cachedSsrEnabled  ||
        hasSelection  != m_cachedHasSelection)
    {
        m_cachedSsaoEnabled  = p.ssaoEnabled;
        m_cachedSsaoShow     = p.ssaoShow;
        m_cachedFxaaEnabled  = p.fxaaEnabled;
        m_cachedSsrEnabled   = p.ssrEnabled;
        m_cachedHasSelection = hasSelection;
        m_graphBuilt = true;
        buildGraph();
    }

    // ── Per-frame uniform uploads ─────────────────────────────────────────────
    if (p.ssaoEnabled) {
        m_ssaoMat->set("uView",          p.viewMat);
        m_ssaoMat->set("uProjection",    p.projMat);
        m_ssaoMat->set("uInvProjection", p.invProjMat);
        m_ssaoMat->set("uNoiseScale",    glm::vec2{
            static_cast<float>(p.fbSize.x) / 4.0f,
            static_cast<float>(p.fbSize.y) / 4.0f});
        m_ssaoMat->set("uRadius", p.ssaoRadius);
        m_ssaoMat->set("uBias",   p.ssaoBias);
    }
    {
        const glm::mat4 invViewProj = glm::inverse(p.projMat * p.viewMat);
        m_deferredMat->set("uInvViewProj",       invViewProj);
        m_deferredMat->set("uShadowBias",        p.shadowBias);
        m_deferredMat->set("uPointShadowCount",  p.shadowLightCount);
        m_deferredMat->set("uPointShadowBias",   p.pointShadowBias);
        for (int c = 0; c < ShadowMaps::NUM_CASCADES; ++c) {
            m_deferredMat->set("uCSMLightSpaceMats[" + std::to_string(c) + "]",
                               m_shadows.csmLightSpaceMats()[c]);
            m_deferredMat->set("uCSMSplitDepths["   + std::to_string(c) + "]",
                               m_shadows.csmSplitDepths()[c]);
        }
    }
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
    }
    m_bloomBrightMat->set("uBloomThreshold", p.bloomThreshold);
    m_tonemapMat->set("uExposure",       p.exposure);
    m_tonemapMat->set("uBloomIntensity", p.bloomIntensity);
    m_tonemapMat->set("uSSRStrength",    p.ssrEnabled ? p.ssrStrength : 0.0f);
    if (p.fxaaEnabled) {
        m_fxaaMat->set("uTexelSize", glm::vec2(1.0f / static_cast<float>(p.fbSize.x),
                                               1.0f / static_cast<float>(p.fbSize.y)));
    }

    m_graph.execute(ctx, p.fbSize);
}
