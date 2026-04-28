#include <sonnet/renderer/frontend/Renderer.h>

#include <glm/glm.hpp>
#include <stdexcept>
#include <string>

namespace {

// ITexture wrapper that looks up the real texture through the Renderer's
// render-target map every call. This means the handle survives resizeRenderTarget:
// when the underlying IRenderTarget is replaced the wrapper automatically sees
// the new textures without any handle invalidation.
using RTMap = std::unordered_map<sonnet::core::RenderTargetHandle,
                                  std::unique_ptr<sonnet::api::render::IRenderTarget>>;

class BorrowedTexture final : public sonnet::api::render::ITexture {
public:
    BorrowedTexture(const RTMap &rts,
                    sonnet::core::RenderTargetHandle rtHandle,
                    int colorIndex)           // -1 = depth attachment
        : m_rts(rts), m_handle(rtHandle), m_colorIndex(colorIndex) {}

    const sonnet::api::render::ITexture *inner() const {
        auto it = m_rts.find(m_handle);
        if (it == m_rts.end()) return nullptr;
        return m_colorIndex >= 0
            ? it->second->colorTexture(static_cast<std::size_t>(m_colorIndex))
            : it->second->depthTexture();
    }

    void bind(std::uint8_t slot)   const override { if (auto *t = inner()) t->bind(slot); }
    void unbind(std::uint8_t slot) const override { if (auto *t = inner()) t->unbind(slot); }

    [[nodiscard]] const sonnet::api::render::TextureDesc &textureDesc() const override { return inner()->textureDesc(); }
    [[nodiscard]] const sonnet::api::render::SamplerDesc &samplerDesc() const override { return inner()->samplerDesc(); }
    [[nodiscard]] unsigned getNativeHandle() const override { return inner() ? inner()->getNativeHandle() : 0u; }
    [[nodiscard]] std::uintptr_t getImGuiTextureId() override {
        auto *t = const_cast<sonnet::api::render::ITexture *>(inner());
        return t ? t->getImGuiTextureId() : 0u;
    }

private:
    const RTMap                          &m_rts;
    sonnet::core::RenderTargetHandle      m_handle;
    int                                   m_colorIndex;
};

} // anonymous namespace

namespace sonnet::renderer::frontend {

using namespace api::render;
using namespace core;

Renderer::Renderer(IRendererBackend &backend) : m_backend(backend) {
    CameraUBO zeroCam{};
    m_cameraUBO = m_backend.createBuffer(BufferType::Uniform, &zeroCam, sizeof(CameraUBO));
    LightsUBO zeroLights{};
    m_lightsUBO = m_backend.createBuffer(BufferType::Uniform, &zeroLights, sizeof(LightsUBO));
}

// ── Asset creation ─────────────────────────────────────────────────────────────

GPUMeshHandle Renderer::createMesh(const CPUMesh &mesh) {
    auto gpuMesh = m_backend.gpuMeshFactory()(mesh);
    GPUMeshHandle handle{m_nextId++};
    m_meshes.emplace(handle, std::move(gpuMesh));
    return handle;
}

ShaderHandle Renderer::createShader(const std::string &vertSrc, const std::string &fragSrc) {
    auto shader = m_backend.shaderCompiler()(vertSrc, fragSrc);
    ShaderHandle handle{m_nextId++};
    m_shaders.emplace(handle, std::move(shader));
    return handle;
}

MaterialTemplateHandle Renderer::createMaterial(const MaterialTemplate &tmpl) {
    MaterialTemplateHandle handle{m_nextId++};
    m_materials.emplace(handle, tmpl);
    return handle;
}

GPUTextureHandle Renderer::createTexture(const TextureDesc &desc,
                                         const SamplerDesc &sampler,
                                         const CPUTextureBuffer &data) {
    auto texture = m_backend.textureFactory().create(desc, sampler, data);
    GPUTextureHandle handle{m_nextId++};
    m_textures.emplace(handle, std::move(texture));
    return handle;
}

RenderTargetHandle Renderer::createRenderTarget(const RenderTargetDesc &desc) {
    auto rt = m_backend.renderTargetFactory().create(desc);
    RenderTargetHandle handle{m_nextId++};
    m_renderTargets.emplace(handle, std::move(rt));
    m_renderTargetDescs.emplace(handle, desc);
    return handle;
}

void Renderer::bindRenderTarget(RenderTargetHandle handle) {
    auto it = m_renderTargets.find(handle);
    if (it == m_renderTargets.end()) return;
    m_backend.bindRenderTarget(*it->second);
}

void Renderer::selectCubemapFace(RenderTargetHandle handle,
                                  std::uint32_t face, std::uint32_t mipLevel) {
    auto it = m_renderTargets.find(handle);
    if (it == m_renderTargets.end()) {
        throw std::invalid_argument("selectCubemapFace: unknown RenderTargetHandle");
    }
    it->second->selectCubemapFace(face, mipLevel);
}

GPUTextureHandle Renderer::depthTextureHandle(RenderTargetHandle handle) {
    if (m_renderTargets.find(handle) == m_renderTargets.end())
        throw std::invalid_argument("depthTextureHandle: unknown RenderTargetHandle");
    if (!m_renderTargets.at(handle)->depthTexture())
        throw std::invalid_argument("depthTextureHandle: render target has no depth texture");

    GPUTextureHandle texHandle{m_nextId++};
    m_textures.emplace(texHandle,
        std::make_unique<BorrowedTexture>(m_renderTargets, handle, -1));
    return texHandle;
}

GPUTextureHandle Renderer::colorTextureHandle(RenderTargetHandle handle, std::size_t colorIndex) {
    if (m_renderTargets.find(handle) == m_renderTargets.end())
        throw std::invalid_argument("colorTextureHandle: unknown RenderTargetHandle");
    if (!m_renderTargets.at(handle)->colorTexture(colorIndex))
        throw std::invalid_argument("colorTextureHandle: render target has no color texture at index");

    GPUTextureHandle texHandle{m_nextId++};
    m_textures.emplace(texHandle,
        std::make_unique<BorrowedTexture>(m_renderTargets, handle, static_cast<int>(colorIndex)));
    return texHandle;
}

void Renderer::resizeRenderTarget(RenderTargetHandle handle,
                                   std::uint32_t width, std::uint32_t height) {
    auto descIt = m_renderTargetDescs.find(handle);
    if (descIt == m_renderTargetDescs.end()) return;
    descIt->second.width  = width;
    descIt->second.height = height;
    m_renderTargets[handle] = m_backend.renderTargetFactory().create(descIt->second);
    // All BorrowedTextures for this handle automatically see the new RT.
}

const api::render::MaterialTemplate *Renderer::getMaterial(core::MaterialTemplateHandle h) const {
    auto it = m_materials.find(h);
    return it != m_materials.end() ? &it->second : nullptr;
}

void Renderer::reloadShader(core::ShaderHandle handle,
                             const std::string &vertSrc,
                             const std::string &fragSrc) {
    auto it = m_shaders.find(handle);
    if (it == m_shaders.end()) return;
    // Compile first — if it throws the old shader stays in place.
    auto newShader = m_backend.shaderCompiler()(vertSrc, fragSrc);
    // Drop any backend-side caches keyed on the old IShader pointer (Vulkan
    // pipelines hold raw VkShader*; the OpenGL backend has nothing to do).
    m_backend.invalidatePipelinesForShader(*it->second);
    it->second = std::move(newShader);
}

// ── IRenderer ──────────────────────────────────────────────────────────────────

GPUTextureHandle Renderer::registerRawTexture(std::unique_ptr<ITexture> tex) {
    GPUTextureHandle handle{m_nextId++};
    m_textures.emplace(handle, std::move(tex));
    return handle;
}

unsigned Renderer::nativeTextureId(GPUTextureHandle handle) const {
    auto it = m_textures.find(handle);
    if (it == m_textures.end())
        throw std::invalid_argument("nativeTextureId: unknown GPUTextureHandle");
    return it->second->getNativeHandle();
}

std::uintptr_t Renderer::imGuiTextureId(GPUTextureHandle handle) {
    auto it = m_textures.find(handle);
    if (it == m_textures.end())
        throw std::invalid_argument("imGuiTextureId: unknown GPUTextureHandle");
    return it->second->getImGuiTextureId();
}

std::array<std::uint8_t, 4> Renderer::readPixelRGBA8(
    RenderTargetHandle handle, std::uint32_t attachmentIndex,
    std::uint32_t x, std::uint32_t y) {
    auto it = m_renderTargets.find(handle);
    if (it == m_renderTargets.end())
        throw std::invalid_argument("readPixelRGBA8: unknown RenderTargetHandle");
    return it->second->readPixelRGBA8(attachmentIndex, x, y);
}

void Renderer::beginFrame() {
    if (m_frameRefCount++ == 0) m_backend.beginFrame();
}

void Renderer::endFrame() {
    if (m_frameRefCount == 0) return; // defensive: unbalanced call.
    if (--m_frameRefCount == 0) m_backend.endFrame();
}

void Renderer::render(const FrameContext &ctx, std::vector<RenderItem> &queue) {
    const CameraUBO camData = buildCameraUBO(ctx);
    m_cameraUBO->update(&camData, sizeof(CameraUBO));
    m_cameraUBO->bindBase(0);

    const LightsUBO lightData = buildLightsUBO(ctx);
    m_lightsUBO->update(&lightData, sizeof(LightsUBO));
    m_lightsUBO->bindBase(1);

    for (const auto &item : queue) {
        auto meshIt = m_meshes.find(item.mesh);
        if (meshIt == m_meshes.end()) continue;

        auto matIt = m_materials.find(item.material.templateHandle());
        if (matIt == m_materials.end()) continue;

        const MaterialTemplate &matTmpl = matIt->second;

        // Apply render state (with optional overrides).
        RenderState state = matTmpl.renderState;
        if (m_overrides) {
            if (m_overrides->fill)      state.fill      = *m_overrides->fill;
            if (m_overrides->depthTest) state.depthTest = *m_overrides->depthTest;
            if (m_overrides->cull)      state.cull      = *m_overrides->cull;
        }
        applyRenderState(state);

        // Bind shader and upload uniforms.
        auto shaderIt = m_shaders.find(matTmpl.shaderHandle);
        if (shaderIt == m_shaders.end()) continue;

        const IShader &shader = *shaderIt->second;
        shader.bind();

        bindMaterial(item.material, ctx, item.modelMatrix);

        // Draw.
        const GpuMesh &mesh = *meshIt->second;
        mesh.bind();
        m_backend.drawIndexed(mesh.indexCount());
        mesh.vertexInputState().unbind();
    }
    queue.clear();
}

// ── Private helpers ────────────────────────────────────────────────────────────

void Renderer::applyRenderState(const RenderState &state) {
    m_backend.setDepthTest(state.depthTest);
    m_backend.setDepthWrite(state.depthWrite);
    m_backend.setDepthFunc(state.depthFunc);
    m_backend.setCull(state.cull);
    m_backend.setFillMode(state.fill);

    switch (state.blend) {
        case BlendMode::Opaque:
            m_backend.setBlend(false);
            break;
        case BlendMode::Alpha:
            m_backend.setBlend(true);
            m_backend.setBlendFunc(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha);
            break;
        case BlendMode::Additive:
            m_backend.setBlend(true);
            m_backend.setBlendFunc(BlendFactor::One, BlendFactor::One);
            break;
    }
}

void Renderer::bindMaterial(const MaterialInstance &mat,
                             const FrameContext &ctx,
                             const glm::mat4 &modelMatrix) {
    auto matIt = m_materials.find(mat.templateHandle());
    if (matIt == m_materials.end()) return;

    const MaterialTemplate &tmpl = matIt->second;
    auto shaderIt = m_shaders.find(tmpl.shaderHandle);
    if (shaderIt == m_shaders.end()) return;

    const IShader &shader = *shaderIt->second;
    const auto &uniforms  = shader.getUniforms();

    // Upload per-draw built-in uniforms. Camera and light data are in UBOs
    // bound once per frame at the top of render() — not uploaded here.
    auto upload = [&](const std::string &name, const core::UniformValue &val) {
        if (auto it = uniforms.find(name); it != uniforms.end()) {
            m_backend.setUniform(it->second.location, val);
        }
    };
    upload("uModel", modelMatrix);

    // Upload light-space matrix if present (forward shadow path, per-draw).
    if (ctx.lightSpaceMatrix) {
        upload("uLightSpaceMatrix", *ctx.lightSpaceMatrix);
    }

    // Upload material uniform values: template defaults first, then per-instance
    // overrides so that instances only need to set values that differ.
    for (const auto &[name, value] : tmpl.defaultValues) {
        upload(name, value);
    }
    for (const auto &[name, value] : mat.values()) {
        upload(name, value);
    }

    // Bind textures — auto-assign slots in iteration order.
    core::Sampler slot = 0;
    for (const auto &[name, texHandle] : mat.getTextures()) {
        auto texIt = m_textures.find(texHandle);
        if (texIt == m_textures.end()) continue;

        if (auto it = uniforms.find(name); it != uniforms.end()) {
            texIt->second->bind(slot);
            m_backend.setUniform(it->second.location, slot);
            ++slot;
        }
    }
}

} // namespace sonnet::renderer::frontend
