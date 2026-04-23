#pragma once

#include <sonnet/api/render/IShader.h>

#include <vulkan/vulkan.h>

#include <array>
#include <string>
#include <vector>

namespace sonnet::renderer::vulkan {

class Device;

// VkShader: owns two VkShaderModules (vertex + fragment) produced from GLSL
// sources via glslang at runtime. Phase 2c scope is compile-to-SPIR-V and
// module creation; descriptor-set layouts, push-constant ranges, vertex-input
// attribute descriptors, and VkPipelineLayout are built in Phase 3 when the
// pipeline cache and descriptor manager come online.
//
// getUniforms() returns an empty map in Phase 2c. Renderer::bindMaterial
// silently skips unknown names, so every setUniform call becomes a no-op
// under Vulkan until Phase 3 — which is fine because Phase 2 has nothing
// to draw anyway.
// Reflected vertex input attribute (built by VkShaderCompiler).
struct ShaderVertexAttribute {
    std::uint32_t location;
    VkFormat      format;
    std::string   name;
};

// Result of reflecting vertex+fragment SPIR-V, passed to VkShader's ctor.
struct ShaderReflection {
    // Per-set bindings, already merged across vertex+fragment stages.
    // Index 0..N-1; missing sets produce an empty VkDescriptorSetLayout
    // (a "null" descriptor set layout so that VkPipelineLayout can still
    // reference higher-numbered sets positionally).
    std::vector<std::vector<VkDescriptorSetLayoutBinding>> setBindings;

    // Merged push-constant ranges (vertex + fragment combined).
    std::vector<VkPushConstantRange>  pushConstantRanges;

    // Vertex input attributes sorted by location.
    std::vector<ShaderVertexAttribute> vertexAttributes;

    // Uniform name → descriptor map (empty until Phase 3c/3d wire it up).
    core::UniformDescriptorMap uniforms;
};

struct BindState;

class VkShader final : public api::render::IShader {
public:
    VkShader(Device &device, BindState &bindState,
             std::string vertexSource, std::string fragmentSource,
             std::vector<std::uint32_t> vertexSpirv,
             std::vector<std::uint32_t> fragmentSpirv,
             ShaderReflection reflection);
    ~VkShader() override;

    VkShader(const VkShader &)            = delete;
    VkShader &operator=(const VkShader &) = delete;

    // IShader
    [[nodiscard]] const std::string                 &getVertexSource()   const override { return m_vertexSource; }
    [[nodiscard]] const std::string                 &getFragmentSource() const override { return m_fragmentSource; }
    [[nodiscard]] const core::ShaderProgram         &getProgram()        const override { return m_programStub; }
    [[nodiscard]] const core::UniformDescriptorMap  &getUniforms()       const override { return m_reflection.uniforms; }

    void bind()   const override; // no-op under Vulkan; pipeline bind happens in drawIndexed.
    void unbind() const override; // no-op under Vulkan.

    // Backend-internal accessors (Phase 3 pipeline cache + descriptor manager).
    [[nodiscard]] VkShaderModule                       vertexModule()   const { return m_vertModule; }
    [[nodiscard]] VkShaderModule                       fragmentModule() const { return m_fragModule; }
    [[nodiscard]] const std::vector<std::uint32_t>    &vertexSpirv()    const { return m_vertSpirv; }
    [[nodiscard]] const std::vector<std::uint32_t>    &fragmentSpirv()  const { return m_fragSpirv; }
    [[nodiscard]] const ShaderReflection              &reflection()    const { return m_reflection; }
    [[nodiscard]] VkPipelineLayout                     pipelineLayout() const { return m_pipelineLayout; }
    [[nodiscard]] const std::vector<VkDescriptorSetLayout> &setLayouts() const { return m_setLayouts; }

private:
    Device                           &m_device;
    BindState                        &m_bindState;
    std::string                       m_vertexSource;
    std::string                       m_fragmentSource;
    std::vector<std::uint32_t>        m_vertSpirv;
    std::vector<std::uint32_t>        m_fragSpirv;
    VkShaderModule                    m_vertModule    = VK_NULL_HANDLE;
    VkShaderModule                    m_fragModule    = VK_NULL_HANDLE;
    ShaderReflection                  m_reflection;
    std::vector<VkDescriptorSetLayout> m_setLayouts;
    VkPipelineLayout                  m_pipelineLayout = VK_NULL_HANDLE;
    core::ShaderProgram               m_programStub    = 0;
};

} // namespace sonnet::renderer::vulkan
