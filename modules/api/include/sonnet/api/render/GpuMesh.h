#pragma once

#include <sonnet/api/render/CPUMesh.h>
#include <sonnet/api/render/IGpuBuffer.h>
#include <sonnet/api/render/IVertexInputState.h>
#include <sonnet/api/render/VertexLayout.h>

#include <cassert>
#include <cstddef>
#include <memory>

namespace sonnet::api::render {

class GpuMesh {
public:
    GpuMesh(VertexLayout layout,
            std::unique_ptr<IGpuBuffer>       vertexBuffer,
            std::unique_ptr<IGpuBuffer>       indexBuffer,
            std::unique_ptr<IVertexInputState> vertexInputState,
            std::size_t indexCount)
        : m_layout(std::move(layout))
        , m_vertexBuffer(std::move(vertexBuffer))
        , m_indexBuffer(std::move(indexBuffer))
        , m_vertexInputState(std::move(vertexInputState))
        , m_indexCount(indexCount) {
        assert(m_vertexBuffer);
        assert(m_indexBuffer);
        assert(m_vertexInputState);
        assert(m_indexCount > 0);
        m_vertexInputState->unbind();
    }

    [[nodiscard]] std::size_t              indexCount()       const { return m_indexCount; }
    [[nodiscard]] const IVertexInputState &vertexInputState() const { return *m_vertexInputState; }

    void bind() const {
        m_vertexInputState->bind();
        m_vertexBuffer->bind();
        m_indexBuffer->bind();
    }

private:
    VertexLayout                       m_layout;
    std::unique_ptr<IGpuBuffer>        m_vertexBuffer;
    std::unique_ptr<IGpuBuffer>        m_indexBuffer;
    std::unique_ptr<IVertexInputState> m_vertexInputState;
    std::size_t                        m_indexCount;
};

class IGpuMeshFactory {
public:
    virtual ~IGpuMeshFactory() = default;
    [[nodiscard]] virtual std::unique_ptr<GpuMesh> operator()(const CPUMesh &mesh) const = 0;
};

} // namespace sonnet::api::render
