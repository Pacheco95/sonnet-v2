#pragma once

#include <sonnet/api/render/FrameContext.h>
#include <sonnet/api/render/Light.h>
#include <sonnet/api/render/Material.h>
#include <sonnet/core/Types.h>
#include <sonnet/renderer/frontend/Renderer.h>
#include <sonnet/renderer/opengl/GlRendererBackend.h>
#include <sonnet/world/Scene.h>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <array>
#include <optional>
#include <vector>

class ShadowMaps {
public:
    static constexpr int   NUM_CASCADES      = 3;
    static constexpr int   MAX_SHADOW_LIGHTS = 4;
    static constexpr int   SHADOW_SIZE       = 2048;
    static constexpr int   POINT_SHADOW_SIZE = 512;
    static constexpr float POINT_SHADOW_FAR  = 25.0f;

    ShadowMaps(sonnet::renderer::frontend::Renderer   &renderer,
               sonnet::renderer::opengl::GlRendererBackend &backend,
               sonnet::core::ShaderHandle shadowShader,
               sonnet::core::ShaderHandle ptShadowShader);

    // Render CSM cascades + point-light shadow cubemaps.
    // Returns the number of point lights that cast shadows.
    int render(const sonnet::world::Scene                            &scene,
               const glm::mat4                                       &viewMat,
               const glm::mat4                                       &projMat,
               float camNear, float camFov, float aspect,
               const std::vector<sonnet::api::render::PointLight>    &pointLights);

    // Outputs available after render().
    const std::array<glm::mat4, NUM_CASCADES>      &csmLightSpaceMats()  const { return m_csmLightSpaceMats; }
    const std::array<float,     NUM_CASCADES>      &csmSplitDepths()     const { return m_csmSplitDepths; }
    const std::array<sonnet::core::GPUTextureHandle, NUM_CASCADES>       &csmDepthHandles()    const { return m_csmDepthHandles; }
    const std::array<sonnet::core::GPUTextureHandle, MAX_SHADOW_LIGHTS>  &pointShadowHandles() const { return m_pointShadowHandles; }

private:
    sonnet::renderer::frontend::Renderer         &m_renderer;
    sonnet::renderer::opengl::GlRendererBackend  &m_backend;

    // CSM resources
    std::array<sonnet::core::RenderTargetHandle, NUM_CASCADES>      m_csmRTHandles{};
    std::array<sonnet::core::GPUTextureHandle,   NUM_CASCADES>      m_csmDepthHandles{};

    // Point-shadow resources (raw GL)
    std::array<GLuint, MAX_SHADOW_LIGHTS>                            m_pointShadowCubeTex{};
    GLuint                                                           m_pointShadowFBO  = 0;
    GLuint                                                           m_pointShadowRBO  = 0;
    std::array<sonnet::core::GPUTextureHandle, MAX_SHADOW_LIGHTS>   m_pointShadowHandles{};

    // Shadow materials
    sonnet::core::MaterialTemplateHandle              m_shadowMatTmpl{};
    sonnet::core::MaterialTemplateHandle              m_ptShadowMatTmpl{};
    std::optional<sonnet::api::render::MaterialInstance> m_shadowMat;
    std::optional<sonnet::api::render::MaterialInstance> m_ptShadowMat;

    // Per-frame outputs (written by render())
    std::array<glm::mat4, NUM_CASCADES> m_csmLightSpaceMats{};
    std::array<float,     NUM_CASCADES> m_csmSplitDepths{};
};
