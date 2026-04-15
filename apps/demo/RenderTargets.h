#pragma once

#include <sonnet/core/Types.h>
#include <sonnet/renderer/frontend/Renderer.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// Plain aggregate of all screen-sized render targets and their derived texture
// handles. Public members are intentional — no invariants to protect.
// Texture handles obtained via colorTextureHandle / depthTextureHandle remain
// valid across resize() calls (BorrowedTexture indirect lookup).
struct RenderTargets {
    RenderTargets(sonnet::renderer::frontend::Renderer &renderer,
                  std::uint32_t w, std::uint32_t h,
                  const std::vector<glm::vec3> &ssaoKernel,
                  sonnet::core::GPUTextureHandle ssaoNoiseHandle);

    // Recreate all RTs at a new resolution. Texture handles remain valid.
    void resize(std::uint32_t w, std::uint32_t h);

    // ── Render target handles ─────────────────────────────────────────────────
    sonnet::core::RenderTargetHandle hdrRT{};
    sonnet::core::RenderTargetHandle gbufRT{};
    sonnet::core::RenderTargetHandle ssaoRT{};
    sonnet::core::RenderTargetHandle ssaoBlurRT{};
    sonnet::core::RenderTargetHandle bloomBrightRT{};
    sonnet::core::RenderTargetHandle bloomBlurRT{};
    sonnet::core::RenderTargetHandle ssrRT{};
    sonnet::core::RenderTargetHandle outlineMaskRT{};
    sonnet::core::RenderTargetHandle pickingRT{};
    sonnet::core::RenderTargetHandle ldrRT{};
    sonnet::core::RenderTargetHandle viewportRT{};

    // ── Texture handles (automatically reflect the current RT after resize) ───
    sonnet::core::GPUTextureHandle hdrTex{};
    sonnet::core::GPUTextureHandle gbufAlbedoRoughTex{};
    sonnet::core::GPUTextureHandle gbufNormalMetallicTex{};
    sonnet::core::GPUTextureHandle gbufEmissiveAOTex{};
    sonnet::core::GPUTextureHandle gbufDepthTex{};
    sonnet::core::GPUTextureHandle ssaoTex{};
    sonnet::core::GPUTextureHandle ssaoBlurTex{};
    sonnet::core::GPUTextureHandle bloomBrightTex{};
    sonnet::core::GPUTextureHandle bloomBlurTex{};
    sonnet::core::GPUTextureHandle ssrTex{};
    sonnet::core::GPUTextureHandle outlineMaskTex{};
    sonnet::core::GPUTextureHandle ldrTex{};
    sonnet::core::GPUTextureHandle viewportTex{};

    // ── SSAO noise texture (registered raw GL texture) ────────────────────────
    sonnet::core::GPUTextureHandle ssaoNoiseTex{};

    // ── Fullscreen quad mesh ──────────────────────────────────────────────────
    sonnet::core::GPUMeshHandle quadMesh{};

    // ── SSAO kernel (non-owning pointer; used by PostProcess at construction) ─
    const std::vector<glm::vec3> *ssaoKernel = nullptr;

private:
    sonnet::renderer::frontend::Renderer &m_renderer;
};
