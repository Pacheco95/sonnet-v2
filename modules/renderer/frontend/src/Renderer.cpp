#include <sonnet/renderer/frontend/Renderer.h>

#include <glm/glm.hpp>
#include <string>

namespace sonnet::renderer::frontend {

using namespace api::render;
using namespace core;

Renderer::Renderer(IRendererBackend &backend) : m_backend(backend) {}

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

// ── IRenderer ──────────────────────────────────────────────────────────────────

void Renderer::beginFrame() {
    m_backend.beginFrame();
}

void Renderer::endFrame() {
    m_backend.endFrame();
}

void Renderer::render(const FrameContext &ctx, std::vector<RenderItem> &queue) {
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

    // Upload built-in uniforms if the shader declares them.
    auto upload = [&](const std::string &name, const core::UniformValue &val) {
        if (auto it = uniforms.find(name); it != uniforms.end()) {
            m_backend.setUniform(it->second.location, val);
        }
    };
    upload("uModel",      modelMatrix);
    upload("uView",       ctx.viewMatrix);
    upload("uProjection", ctx.projectionMatrix);

    // Upload directional light if present.
    if (ctx.directionalLight) {
        const auto &dl = *ctx.directionalLight;
        upload("uDirLight.direction", dl.direction);
        upload("uDirLight.color",     dl.color);
        upload("uDirLight.intensity", dl.intensity);
    }

    // Upload point lights if present.
    if (!ctx.pointLights.empty()) {
        upload("uPointLightCount", static_cast<int>(ctx.pointLights.size()));
        for (std::size_t i = 0; i < ctx.pointLights.size(); ++i) {
            const auto &pl     = ctx.pointLights[i];
            const std::string  p = "uPointLights[" + std::to_string(i) + "].";
            upload(p + "position",  pl.position);
            upload(p + "color",     pl.color);
            upload(p + "intensity", pl.intensity);
            upload(p + "constant",  pl.constant);
            upload(p + "linear",    pl.linear);
            upload(p + "quadratic", pl.quadratic);
        }
    }

    // Upload material uniform values.
    for (const auto &[name, value] : mat.values()) {
        upload(name, value);
    }

    // Bind textures.
    for (const auto &[name, texHandle] : mat.getTextures()) {
        auto texIt = m_textures.find(texHandle);
        if (texIt == m_textures.end()) continue;

        if (auto it = uniforms.find(name); it != uniforms.end()) {
            const Sampler slot = std::get<Sampler>(mat.values().at(name));
            texIt->second->bind(slot);
        }
    }
}

} // namespace sonnet::renderer::frontend
