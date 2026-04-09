#pragma once

#include <sonnet/api/render/GpuMesh.h>
#include <sonnet/api/render/IRendererBackend.h>
#include <sonnet/core/Macros.h>

#include <memory>

namespace sonnet::renderer::opengl {

class GlGpuMeshFactory final : public api::render::IGpuMeshFactory {
public:
    explicit GlGpuMeshFactory(api::render::IRendererBackend &backend)
        : m_backend(backend) {}

    SN_NON_COPYABLE(GlGpuMeshFactory);
    SN_NON_MOVABLE(GlGpuMeshFactory);

    [[nodiscard]] std::unique_ptr<api::render::GpuMesh> operator()(
        const api::render::CPUMesh &mesh) const override {
        using api::render::BufferType;

        auto vbo = m_backend.createBuffer(BufferType::Vertex,
                                          mesh.rawData().data(),
                                          mesh.bytes());
        auto ibo = m_backend.createBuffer(BufferType::Index,
                                          mesh.indices().data(),
                                          mesh.indices().size() * sizeof(api::render::CPUMesh::Index));
        auto vao = m_backend.createVertexInputState(mesh.layout(), *vbo, *ibo);

        return std::make_unique<api::render::GpuMesh>(
            mesh.layout(),
            std::move(vbo),
            std::move(ibo),
            std::move(vao),
            mesh.indices().size());
    }

private:
    api::render::IRendererBackend &m_backend;
};

} // namespace sonnet::renderer::opengl
