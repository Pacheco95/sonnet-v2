#include "RenderTargets.h"

#include <sonnet/api/render/IRenderTarget.h>
#include <sonnet/primitives/MeshPrimitives.h>

using namespace sonnet::api::render;

RenderTargets::RenderTargets(sonnet::renderer::frontend::Renderer &renderer,
                              std::uint32_t w, std::uint32_t h,
                              const std::vector<glm::vec3> &kernel,
                              sonnet::core::GPUTextureHandle ssaoNoiseHandle)
    : m_renderer(renderer)
{
    ssaoKernel  = &kernel;
    ssaoNoiseTex = ssaoNoiseHandle;

    const SamplerDesc nearestClamp{
        .minFilter = MinFilter::Nearest, .magFilter = MagFilter::Nearest,
        .wrapS = TextureWrap::ClampToEdge, .wrapT = TextureWrap::ClampToEdge,
    };
    const SamplerDesc linearClamp{
        .minFilter = MinFilter::Linear, .magFilter = MagFilter::Linear,
        .wrapS = TextureWrap::ClampToEdge, .wrapT = TextureWrap::ClampToEdge,
    };

    // ── HDR render target ─────────────────────────────────────────────────────
    hdrRT  = renderer.createRenderTarget(RenderTargetDesc{
        .width = w, .height = h,
        .colors = {{ .format = TextureFormat::RGBA16F, .samplerDesc = linearClamp }},
        .depth  = RenderBufferDesc{},
    });
    hdrTex = renderer.colorTextureHandle(hdrRT, 0);

    // ── G-buffer (3 × RGBA16F colour + Depth24 texture) ──────────────────────
    gbufRT = renderer.createRenderTarget(RenderTargetDesc{
        .width = w, .height = h,
        .colors = {
            { .format = TextureFormat::RGBA16F, .samplerDesc = nearestClamp },
            { .format = TextureFormat::RGBA16F, .samplerDesc = nearestClamp },
            { .format = TextureFormat::RGBA16F, .samplerDesc = nearestClamp },
        },
        .depth = TextureAttachmentDesc{
            .format = TextureFormat::Depth24, .samplerDesc = linearClamp,
        },
    });
    gbufAlbedoRoughTex    = renderer.colorTextureHandle(gbufRT, 0);
    gbufNormalMetallicTex = renderer.colorTextureHandle(gbufRT, 1);
    gbufEmissiveAOTex     = renderer.colorTextureHandle(gbufRT, 2);
    gbufDepthTex          = renderer.depthTextureHandle(gbufRT);

    // ── SSAO targets (R32F) ───────────────────────────────────────────────────
    auto makeR32FRT = [&]() {
        return renderer.createRenderTarget(RenderTargetDesc{
            .width = w, .height = h,
            .colors = {{ .format = TextureFormat::R32F, .samplerDesc = linearClamp }},
        });
    };
    ssaoRT      = makeR32FRT();
    ssaoBlurRT  = makeR32FRT();
    ssaoTex     = renderer.colorTextureHandle(ssaoRT,     0);
    ssaoBlurTex = renderer.colorTextureHandle(ssaoBlurRT, 0);

    // ── LDR target (FXAA reads from this) ────────────────────────────────────
    ldrRT  = renderer.createRenderTarget(RenderTargetDesc{
        .width = w, .height = h,
        .colors = {{ .format = TextureFormat::RGBA8, .samplerDesc = linearClamp }},
    });
    ldrTex = renderer.colorTextureHandle(ldrRT, 0);

    // ── Viewport target (ImGui Viewport panel) ────────────────────────────────
    viewportRT  = renderer.createRenderTarget(RenderTargetDesc{
        .width = w, .height = h,
        .colors = {{ .format = TextureFormat::RGBA8, .samplerDesc = linearClamp }},
    });
    viewportTex = renderer.colorTextureHandle(viewportRT, 0);

    // ── Bloom targets (RGBA16F) ───────────────────────────────────────────────
    auto makeBloomRT = [&]() {
        return renderer.createRenderTarget(RenderTargetDesc{
            .width = w, .height = h,
            .colors = {{ .format = TextureFormat::RGBA16F, .samplerDesc = linearClamp }},
        });
    };
    bloomBrightRT  = makeBloomRT();
    bloomBlurRT    = makeBloomRT();
    bloomBrightTex = renderer.colorTextureHandle(bloomBrightRT, 0);
    bloomBlurTex   = renderer.colorTextureHandle(bloomBlurRT,   0);

    // ── SSR target (reuses RGBA16F bloom format) ──────────────────────────────
    ssrRT  = makeBloomRT();
    ssrTex = renderer.colorTextureHandle(ssrRT, 0);

    // ── Outline mask (RGBA8, no depth — full silhouette) ──────────────────────
    outlineMaskRT  = renderer.createRenderTarget(RenderTargetDesc{
        .width = w, .height = h,
        .colors = {{ .format = TextureFormat::RGBA8, .samplerDesc = nearestClamp }},
    });
    outlineMaskTex = renderer.colorTextureHandle(outlineMaskRT, 0);

    // ── Picking target (RGBA8 + depth for occlusion) ──────────────────────────
    pickingRT = renderer.createRenderTarget(RenderTargetDesc{
        .width = w, .height = h,
        .colors = {{ .format = TextureFormat::RGBA8, .samplerDesc = nearestClamp }},
        .depth  = RenderBufferDesc{},
    });

    // ── Fullscreen quad ───────────────────────────────────────────────────────
    quadMesh = renderer.createMesh(sonnet::primitives::makeQuad({2.0f, 2.0f}));
}

void RenderTargets::resize(std::uint32_t w, std::uint32_t h) {
    const sonnet::core::RenderTargetHandle kRTs[] = {
        hdrRT, gbufRT, ssaoRT, ssaoBlurRT,
        ldrRT, viewportRT, bloomBrightRT, bloomBlurRT, ssrRT,
        outlineMaskRT, pickingRT,
    };
    for (auto handle : kRTs)
        m_renderer.resizeRenderTarget(handle, w, h);
}
