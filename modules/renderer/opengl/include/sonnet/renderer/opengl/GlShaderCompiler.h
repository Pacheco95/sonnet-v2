#pragma once

#include <sonnet/api/render/IShader.h>
#include <sonnet/core/Macros.h>

#include <memory>
#include <string>

namespace sonnet::renderer::opengl {

class GlShaderCompiler final : public api::render::IShaderCompiler {
public:
    GlShaderCompiler() = default;

    SN_NON_COPYABLE(GlShaderCompiler);
    SN_NON_MOVABLE(GlShaderCompiler);

    [[nodiscard]] std::unique_ptr<api::render::IShader> operator()(
        const std::string &vertexSrc, const std::string &fragmentSrc) const override;
};

} // namespace sonnet::renderer::opengl
