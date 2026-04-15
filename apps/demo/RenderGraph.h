#pragma once

#include <sonnet/api/render/FrameContext.h>
#include <sonnet/core/Types.h>
#include <sonnet/renderer/frontend/Renderer.h>
#include <sonnet/renderer/opengl/GlRendererBackend.h>

#include <glm/glm.hpp>

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// ── Clear descriptor ──────────────────────────────────────────────────────────
// Describes how a render target should be cleared at the start of a pass.
// An empty RGClearDesc (no colors, no depth) means the pass does not clear.
struct RGAttachmentClear {
    int       index; // color attachment index
    glm::vec4 color;
};

struct RGClearDesc {
    std::vector<RGAttachmentClear> colors;
    std::optional<float>           depth; // nullopt = don't clear depth
};

// ── RenderGraph ───────────────────────────────────────────────────────────────
// Declarative frame graph for the demo post-process pipeline.
//
// Usage:
//   1. registerTexSource(rt, tex) — tell the graph which RT produces each tex.
//   2. addPass(...)               — register passes with read/write declarations.
//   3. compile()                  — topological sort + dead-pass culling.
//   4. execute(ctx, frameSize)    — run all live passes every frame.
//   5. reset()                    — clear all passes before rebuilding.
//
// Dependency model:
//   - A pass that reads texture T depends on the first pass that declared
//     writesRT(RT) where registerTexSource(RT, T) was called.
//   - Passes with isOutput=true are never culled; all transitive dependencies
//     of output passes are kept live.
class RenderGraph {
public:
    // Callback signature: receives the scene FrameContext and a fullscreen-quad
    // FrameContext (identity matrices, viewport = frameSize).
    using ExecuteFn = std::function<void(
        const sonnet::api::render::FrameContext &ctx,
        const sonnet::api::render::FrameContext &ppCtx)>;

    RenderGraph(sonnet::renderer::frontend::Renderer        &renderer,
                sonnet::renderer::opengl::GlRendererBackend &backend);

    // ── Registration ─────────────────────────────────────────────────────────

    // Record that texture 'tex' is produced by render target 'rt'.
    // Must be called before addPass() for dependency resolution to work.
    void registerTexSource(sonnet::core::RenderTargetHandle rt,
                           sonnet::core::GPUTextureHandle   tex);

    // Register a pass that binds 'writesRT', optionally clears it per 'clear',
    // and then calls 'execute'. 'reads' lists texture handles this pass samples
    // from (used to derive execution order). 'isOutput' prevents culling.
    void addPass(std::string                                  name,
                 std::vector<sonnet::core::GPUTextureHandle>  reads,
                 sonnet::core::RenderTargetHandle             writesRT,
                 RGClearDesc                                  clear,
                 bool                                         isOutput,
                 ExecuteFn                                    execute);

    // ── Compile ───────────────────────────────────────────────────────────────
    // Topological sort + dead-pass culling. Call after all addPass() calls.
    // Throws std::runtime_error on a dependency cycle.
    void compile();

    // ── Execute ───────────────────────────────────────────────────────────────
    // Run all live passes in dependency order. Binds each pass's render target,
    // applies the declared clear, sets the viewport, then calls the callback.
    void execute(const sonnet::api::render::FrameContext &ctx,
                 glm::ivec2 frameSize);

    // ── Reset ─────────────────────────────────────────────────────────────────
    // Remove all passes and tex→RT mappings. Call before re-registering.
    void reset();

private:
    struct PassNode {
        std::string                                    name;
        std::vector<sonnet::core::GPUTextureHandle>    reads;
        sonnet::core::RenderTargetHandle               writesRT{};
        RGClearDesc                                    clear;
        bool                                           isOutput = false;
        ExecuteFn                                      execute;
    };

    sonnet::renderer::frontend::Renderer        &m_renderer;
    sonnet::renderer::opengl::GlRendererBackend &m_backend;

    std::vector<PassNode>    m_passes;

    // tex handle → first RT that declared it as an output (first-writer wins).
    std::unordered_map<sonnet::core::GPUTextureHandle,
                       sonnet::core::RenderTargetHandle> m_texToRT;

    // Indices into m_passes in execution order (live passes only).
    std::vector<std::size_t> m_sortedOrder;
};
