#include "RenderGraph.h"

#include <sonnet/api/render/IRendererBackend.h>

#include <glad/glad.h>

#include <algorithm>
#include <limits>
#include <queue>
#include <set>
#include <stdexcept>

using namespace sonnet::api::render;

RenderGraph::RenderGraph(sonnet::renderer::frontend::Renderer        &renderer,
                          sonnet::renderer::opengl::GlRendererBackend &backend)
    : m_renderer(renderer), m_backend(backend)
{}

void RenderGraph::registerTexSource(sonnet::core::RenderTargetHandle rt,
                                     sonnet::core::GPUTextureHandle   tex)
{
    // First-writer wins: don't overwrite an existing mapping.
    m_texToRT.try_emplace(tex, rt);
}

void RenderGraph::addPass(std::string                                  name,
                           std::vector<sonnet::core::GPUTextureHandle>  reads,
                           sonnet::core::RenderTargetHandle             writesRT,
                           RGClearDesc                                  clear,
                           bool                                         isOutput,
                           ExecuteFn                                    execute)
{
    m_passes.push_back({
        std::move(name),
        std::move(reads),
        writesRT,
        std::move(clear),
        isOutput,
        std::move(execute),
    });
}

void RenderGraph::compile()
{
    constexpr std::size_t NONE = std::numeric_limits<std::size_t>::max();
    const std::size_t     N    = m_passes.size();

    // ── Build writerOf: RT → first pass that declared writesRT on it ──────────
    std::unordered_map<sonnet::core::RenderTargetHandle, std::size_t> writerOf;
    for (std::size_t i = 0; i < N; ++i)
        if (m_passes[i].writesRT.isValid())
            writerOf.try_emplace(m_passes[i].writesRT, i); // first-writer wins

    // Resolve a texture read to the pass index that produced it.
    auto resolveWriter = [&](sonnet::core::GPUTextureHandle tex) -> std::size_t {
        auto it = m_texToRT.find(tex);
        if (it == m_texToRT.end()) return NONE;
        auto jt = writerOf.find(it->second);
        if (jt == writerOf.end()) return NONE;
        return jt->second;
    };

    // ── Build deduplicated edges: writer → reader ─────────────────────────────
    std::vector<std::vector<std::size_t>> successors(N);
    std::vector<int>                      inDegree(N, 0);
    for (std::size_t i = 0; i < N; ++i) {
        std::set<std::size_t> deps;
        for (const auto &tex : m_passes[i].reads) {
            std::size_t w = resolveWriter(tex);
            if (w != NONE && w != i) deps.insert(w); // skip unresolved + self-loops
        }
        for (std::size_t w : deps) {
            successors[w].push_back(i);
            ++inDegree[i];
        }
    }

    // ── Mark liveness: BFS backwards from output passes ───────────────────────
    std::vector<std::vector<std::size_t>> predecessors(N);
    for (std::size_t i = 0; i < N; ++i)
        for (std::size_t j : successors[i])
            predecessors[j].push_back(i);

    std::vector<bool>     live(N, false);
    std::queue<std::size_t> lq;
    for (std::size_t i = 0; i < N; ++i)
        if (m_passes[i].isOutput) { live[i] = true; lq.push(i); }
    while (!lq.empty()) {
        std::size_t cur = lq.front(); lq.pop();
        for (std::size_t pred : predecessors[cur])
            if (!live[pred]) { live[pred] = true; lq.push(pred); }
    }

    // ── Kahn's topological sort on the live subgraph ──────────────────────────
    std::queue<std::size_t> q;
    for (std::size_t i = 0; i < N; ++i)
        if (live[i] && inDegree[i] == 0) q.push(i);

    m_sortedOrder.clear();
    while (!q.empty()) {
        std::size_t cur = q.front(); q.pop();
        if (!live[cur]) continue;
        m_sortedOrder.push_back(cur);
        for (std::size_t next : successors[cur]) {
            if (!live[next]) continue;
            if (--inDegree[next] == 0) q.push(next);
        }
    }

    // Detect cycles in the live subgraph.
    std::size_t liveCount = 0;
    for (std::size_t i = 0; i < N; ++i) if (live[i]) ++liveCount;
    if (m_sortedOrder.size() != liveCount)
        throw std::runtime_error("RenderGraph: dependency cycle detected");
}

void RenderGraph::execute(const FrameContext &ctx, glm::ivec2 frameSize)
{
    // Fullscreen-quad context: identity matrices, viewport = frame size.
    const FrameContext ppCtx{
        .viewMatrix       = glm::mat4{1.0f},
        .projectionMatrix = glm::mat4{1.0f},
        .viewPosition     = glm::vec3{0.0f},
        .viewportWidth    = static_cast<std::uint32_t>(frameSize.x),
        .viewportHeight   = static_cast<std::uint32_t>(frameSize.y),
        .deltaTime        = 0.0f,
    };

    for (std::size_t idx : m_sortedOrder) {
        const PassNode &pass = m_passes[idx];

        if (pass.writesRT.isValid()) {
            m_renderer.bindRenderTarget(pass.writesRT);
            m_backend.setViewport(static_cast<std::uint32_t>(frameSize.x),
                                  static_cast<std::uint32_t>(frameSize.y));

            if (!pass.clear.colors.empty() || pass.clear.depth.has_value()) {
                if (pass.clear.depth.has_value())
                    glDepthMask(GL_TRUE);

                ClearOptions opts;
                for (const auto &c : pass.clear.colors)
                    opts.colors.push_back({static_cast<std::uint32_t>(c.index), c.color});
                opts.depth = pass.clear.depth;
                m_backend.clear(opts);
            }
        }

        pass.execute(ctx, ppCtx);
    }
}

void RenderGraph::reset()
{
    m_passes.clear();
    m_texToRT.clear();
    m_sortedOrder.clear();
}
