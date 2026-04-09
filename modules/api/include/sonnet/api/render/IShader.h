#pragma once

#include <sonnet/core/Types.h>

#include <memory>
#include <string>

namespace sonnet::api::render {

class IShader {
public:
    virtual ~IShader() = default;

    [[nodiscard]] virtual const std::string              &getVertexSource()   const = 0;
    [[nodiscard]] virtual const std::string              &getFragmentSource() const = 0;
    [[nodiscard]] virtual const core::ShaderProgram      &getProgram()        const = 0;
    [[nodiscard]] virtual const core::UniformDescriptorMap &getUniforms()     const = 0;

    virtual void bind()   const = 0;
    virtual void unbind() const = 0;
};

class IShaderCompiler {
public:
    virtual ~IShaderCompiler() = default;
    [[nodiscard]] virtual std::unique_ptr<IShader> operator()(
        const std::string &vertexSrc, const std::string &fragmentSrc) const = 0;
};

} // namespace sonnet::api::render
