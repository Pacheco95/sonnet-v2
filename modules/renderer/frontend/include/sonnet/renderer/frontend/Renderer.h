#pragma once

#include <sonnet/api/render/IRenderer.h>
#include <sonnet/api/render/IRendererBackend.h>
#include <sonnet/api/render/Material.h>
#include <sonnet/api/render/RenderItem.h>
#include <sonnet/core/Macros.h>
#include <sonnet/core/Types.h>

#include <memory>
#include <unordered_map>

namespace sonnet::renderer::frontend {

// High-level renderer: resolves typed handles, applies render state,
// and issues draw calls via IRendererBackend.
class Renderer final : public api::render::IRenderer {
public:
    explicit Renderer(api::render::IRendererBackend &backend);
    ~Renderer() override = default;

    SN_NON_COPYABLE(Renderer);
    SN_NON_MOVABLE(Renderer);

    // ── Asset creation ────────────────────────────────────────────────────────
    [[nodiscard]] core::GPUMeshHandle            createMesh(const api::render::CPUMesh &mesh);
    [[nodiscard]] core::ShaderHandle             createShader(const std::string &vertSrc,
                                                              const std::string &fragSrc);
    [[nodiscard]] core::MaterialTemplateHandle   createMaterial(const api::render::MaterialTemplate &tmpl);
    [[nodiscard]] core::GPUTextureHandle         createTexture(const api::render::TextureDesc &desc,
                                                               const api::render::SamplerDesc &sampler,
                                                               const api::render::CPUTextureBuffer &data);

    // ── IRenderer ─────────────────────────────────────────────────────────────
    void beginFrame() override;
    void render(const api::render::FrameContext &ctx,
                std::vector<api::render::RenderItem> &queue) override;
    void endFrame()   override;
    void setOverrides(api::render::RenderOverrides *overrides) override { m_overrides = overrides; }

private:
    void applyRenderState(const api::render::RenderState &state);
    void bindMaterial(const api::render::MaterialInstance &mat,
                      const api::render::FrameContext &ctx,
                      const glm::mat4 &modelMatrix);

    api::render::IRendererBackend   &m_backend;
    api::render::RenderOverrides    *m_overrides = nullptr;

    // Asset stores keyed by typed handles.
    std::unordered_map<core::GPUMeshHandle,          std::unique_ptr<api::render::GpuMesh>>         m_meshes;
    std::unordered_map<core::ShaderHandle,           std::unique_ptr<api::render::IShader>>          m_shaders;
    std::unordered_map<core::MaterialTemplateHandle, api::render::MaterialTemplate>                  m_materials;
    std::unordered_map<core::GPUTextureHandle,       std::unique_ptr<api::render::ITexture>>         m_textures;

    std::size_t m_nextId = 1; // monotonic id for handle generation
};

} // namespace sonnet::renderer::frontend
