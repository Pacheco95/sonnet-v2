#pragma once

#include <sonnet/api/render/IShader.h>

#include <memory>

namespace sonnet::renderer::vulkan {

class Device;

// Runtime GLSL → SPIR-V via glslang (C interface from the Vulkan SDK).
// One instance per VkRendererBackend; calls glslang_initialize_process on
// construction and glslang_finalize_process on destruction. Compilation
// failures throw VulkanError carrying the glslang log.
class VkShaderCompiler final : public api::render::IShaderCompiler {
public:
    explicit VkShaderCompiler(Device &device);
    ~VkShaderCompiler() override;

    VkShaderCompiler(const VkShaderCompiler &)            = delete;
    VkShaderCompiler &operator=(const VkShaderCompiler &) = delete;

    [[nodiscard]] std::unique_ptr<api::render::IShader> operator()(
        const std::string &vertexSrc, const std::string &fragmentSrc) const override;

private:
    Device &m_device;
};

} // namespace sonnet::renderer::vulkan
