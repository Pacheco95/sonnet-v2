#pragma once

#include <cstddef>
#include <cstdint>

namespace sonnet::api::render {

enum class BufferType { Vertex, Index, Uniform };

class IGpuBuffer {
public:
    virtual ~IGpuBuffer() = default;
    virtual void bind()                                           const = 0;
    virtual void update(const void *data, std::size_t size)           = 0;
    virtual void bindBase(std::uint32_t bindingPoint)             const = 0;
};

} // namespace sonnet::api::render
