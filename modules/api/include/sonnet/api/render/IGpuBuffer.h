#pragma once

namespace sonnet::api::render {

enum class BufferType { Vertex, Index, Uniform };

class IGpuBuffer {
public:
    virtual ~IGpuBuffer() = default;
    virtual void bind() const = 0;
};

} // namespace sonnet::api::render
