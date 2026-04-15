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
    [[nodiscard]] core::RenderTargetHandle       createRenderTarget(const api::render::RenderTargetDesc &desc);
    void                                         bindRenderTarget(core::RenderTargetHandle handle);
    // Returns a GPUTextureHandle that borrows the render target's color attachment.
    // The handle is invalidated if the render target is destroyed.
    [[nodiscard]] core::GPUTextureHandle         colorTextureHandle(core::RenderTargetHandle handle,
                                                                    std::size_t colorIndex = 0);
    [[nodiscard]] core::GPUTextureHandle         depthTextureHandle(core::RenderTargetHandle handle);
    // Register an externally-created ITexture (e.g. IBL cubemaps) and return a handle.
    [[nodiscard]] core::GPUTextureHandle         registerRawTexture(std::unique_ptr<api::render::ITexture> tex);
    // Return the backend-native texture id (e.g. GLuint) for a texture handle.
    // Useful for passing render-target textures to ImGui::Image.
    [[nodiscard]] unsigned                       nativeTextureId(core::GPUTextureHandle handle) const;

    // Look up a MaterialTemplate by handle. Returns nullptr if not found.
    // Used by editor UI to display/edit material uniforms generically.
    [[nodiscard]] const api::render::MaterialTemplate *getMaterial(
        core::MaterialTemplateHandle h) const;

    // Recompile a shader in-place. The handle and all MaterialTemplates that
    // reference it remain valid. If compilation fails the old shader is kept
    // and the exception is re-thrown so the caller can log it.
    void reloadShader(core::ShaderHandle handle,
                      const std::string &vertSrc,
                      const std::string &fragSrc);

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
    std::unordered_map<core::RenderTargetHandle,     std::unique_ptr<api::render::IRenderTarget>>    m_renderTargets;

    std::size_t m_nextId = 1; // monotonic id for handle generation
};

} // namespace sonnet::renderer::frontend
