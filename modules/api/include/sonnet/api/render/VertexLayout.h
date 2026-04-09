#pragma once

#include <sonnet/api/render/VertexAttribute.h>
#include <sonnet/core/Hash.h>

namespace sonnet::api::render {

class VertexLayout {
public:
    explicit VertexLayout(KnownAttributeSet attributes)
        : m_attributes(std::move(attributes)) {
        for (const auto &attr : m_attributes) {
            std::visit([&](auto &&a) { m_stride += std::decay_t<decltype(a)>::sizeInBytes; }, attr);
        }
    }

    [[nodiscard]] std::size_t              getStride()     const { return m_stride; }
    [[nodiscard]] const KnownAttributeSet &getAttributes() const { return m_attributes; }

    struct Hasher {
        std::size_t operator()(const VertexLayout &v) const noexcept {
            std::size_t seed = 0;
            core::hashCombine(seed, v.m_stride);
            return seed;
        }
    };

private:
    KnownAttributeSet m_attributes;
    std::size_t       m_stride{0};
};

} // namespace sonnet::api::render
