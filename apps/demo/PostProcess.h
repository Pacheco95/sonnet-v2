#pragma once

#include "IBL.h"
#include "RenderTargets.h"
#include "ShadowMaps.h"
#include "ShaderRegistry.h"

#include <sonnet/api/render/FrameContext.h>
#include <sonnet/api/render/Material.h>
#include <sonnet/core/Types.h>
#include <sonnet/renderer/opengl/GlRendererBackend.h>
#include <sonnet/world/GameObject.h>
#include <sonnet/world/Scene.h>

#include <glm/glm.hpp>

#include <optional>

// Parameters forwarded to PostProcess::execute() each frame.
struct PostProcessParams {
    // Camera / viewport
    glm::mat4  viewMat{};
    glm::mat4  projMat{};
    glm::mat4  invProjMat{};
    glm::ivec2 fbSize{};

    // Shadow results (from ShadowMaps::render())
    int   shadowLightCount = 0;
    float shadowBias       = 0.005f;
    float pointShadowBias  = 0.008f;

    // Tone-mapping / bloom
    float exposure        = 1.0f;
    float bloomThreshold  = 0.8f;
    float bloomIntensity  = 0.5f;
    int   bloomIterations = 3;

    // SSAO
    bool  ssaoEnabled = true;
    bool  ssaoShow    = false;
    float ssaoRadius  = 1.5f;
    float ssaoBias    = 0.05f;

    // Post-process toggles
    bool      fxaaEnabled    = true;
    bool      outlineEnabled = true;
    glm::vec3 outlineColor{1.0f, 0.6f, 0.05f};

    // SSR
    bool  ssrEnabled      = true;
    int   ssrMaxSteps     = 64;
    float ssrStepSize     = 0.1f;
    float ssrThickness    = 0.2f;
    float ssrMaxDistance  = 10.0f;
    float ssrRoughnessMax = 0.4f;
    float ssrStrength     = 1.0f;

    // Scene state (for outline mask and indicator spheres)
    const sonnet::world::Scene      *scene          = nullptr;
    const sonnet::world::GameObject *selectedObject = nullptr;
};

class PostProcess {
public:
    PostProcess(sonnet::renderer::frontend::Renderer         &renderer,
                sonnet::renderer::opengl::GlRendererBackend  &backend,
                ShaderRegistry                               &shaders,
                const RenderTargets                          &rts,
                const ShadowMaps                             &shadows,
                const IBLMaps                                &ibl,
                sonnet::core::GPUMeshHandle                   quadMesh,
                sonnet::core::GPUMeshHandle                   sphereMesh,
                sonnet::core::MaterialTemplateHandle          emissiveMatTmpl);

    // Run all post-process passes for one frame.
    void execute(const PostProcessParams &p,
                 const sonnet::api::render::FrameContext &ctx);

    // ── Public template handles (used by EditorUI for picking pass) ───────────
    sonnet::core::MaterialTemplateHandle pickingMatTmpl{};
    sonnet::core::MaterialTemplateHandle pickingSkinnedMatTmpl{};

private:
    void fullscreenQuad(sonnet::api::render::MaterialInstance &mat,
                        const sonnet::api::render::FrameContext &ppCtx);

    sonnet::renderer::frontend::Renderer        &m_renderer;
    sonnet::renderer::opengl::GlRendererBackend &m_backend;
    const RenderTargets                         &m_rts;
    const ShadowMaps                            &m_shadows;
    sonnet::core::GPUMeshHandle                  m_quadMesh{};
    sonnet::core::GPUMeshHandle                  m_sphereMesh{};
    sonnet::core::MaterialTemplateHandle         m_emissiveMatTmpl{};
    sonnet::core::MaterialTemplateHandle         m_outlineMaskSkinnedMatTmpl{};

    // All post-process material instances (initialized in constructor).
    std::optional<sonnet::api::render::MaterialInstance> m_bloomBrightMat;
    std::optional<sonnet::api::render::MaterialInstance> m_bloomBlurHMat;
    std::optional<sonnet::api::render::MaterialInstance> m_bloomBlurVMat;
    std::optional<sonnet::api::render::MaterialInstance> m_ssrMat;
    std::optional<sonnet::api::render::MaterialInstance> m_tonemapMat;
    std::optional<sonnet::api::render::MaterialInstance> m_fxaaMat;
    std::optional<sonnet::api::render::MaterialInstance> m_ssaoMat;
    std::optional<sonnet::api::render::MaterialInstance> m_ssaoBlurMat;
    std::optional<sonnet::api::render::MaterialInstance> m_ssaoShowMat;
    std::optional<sonnet::api::render::MaterialInstance> m_outlineMaskMat;
    std::optional<sonnet::api::render::MaterialInstance> m_outlineMat;
    std::optional<sonnet::api::render::MaterialInstance> m_skyMat;
    std::optional<sonnet::api::render::MaterialInstance> m_deferredMat;
};
