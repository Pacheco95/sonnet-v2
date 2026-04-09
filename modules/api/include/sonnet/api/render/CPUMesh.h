#pragma once

#include <sonnet/api/render/VertexLayout.h>
#include <sonnet/core/Hash.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace sonnet::api::render {

class CPUMesh {
public:
    using Index = std::uint32_t;

    explicit CPUMesh(VertexLayout layout, std::vector<Index> indices,
                     std::optional<std::size_t> vertexCountHint = std::nullopt)
        : m_layout(std::move(layout)), m_indices(std::move(indices)) {
        if (vertexCountHint) {
            m_buffer.reserve(m_layout.getStride() * *vertexCountHint);
        }
    }

    // Append a single vertex given its attributes in layout order.
    CPUMesh &addVertex(const KnownAttributeSet &attrs) {
        for (const auto &attr : attrs) {
            std::visit([&](auto &&a) {
                using A        = std::decay_t<decltype(a)>;
                const auto *p  = reinterpret_cast<const std::byte *>(&a.value);
                m_buffer.insert(m_buffer.end(), p, p + A::sizeInBytes);
            }, attr);
        }
        ++m_vertexCount;
        return *this;
    }

    [[nodiscard]] const std::vector<std::byte> &rawData()     const { return m_buffer; }
    [[nodiscard]] const std::vector<Index>     &indices()     const { return m_indices; }
    [[nodiscard]] const VertexLayout           &layout()      const { return m_layout; }
    [[nodiscard]] std::size_t                   vertexCount() const { return m_vertexCount; }
    [[nodiscard]] std::size_t                   bytes()       const { return m_buffer.size(); }

    struct Hasher {
        std::size_t operator()(const CPUMesh &v) const noexcept {
            std::size_t seed = 0;
            core::hashCombine(seed, v.m_vertexCount);
            core::hashCombine(seed, v.m_buffer);
            core::hashCombine(seed, v.m_indices);
            return seed;
        }
    };

private:
    VertexLayout        m_layout;
    std::vector<Index>  m_indices;
    std::vector<std::byte> m_buffer{};
    std::size_t         m_vertexCount{0};
};

} // namespace sonnet::api::render
