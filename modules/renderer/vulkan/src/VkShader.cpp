#include <sonnet/renderer/vulkan/VkShader.h>

#include "VkDevice.h"
#include "VkUtils.h"

namespace sonnet::renderer::vulkan {

namespace {

VkShaderModule createModule(VkDevice device, const std::vector<std::uint32_t> &spirv) {
    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spirv.size() * sizeof(std::uint32_t);
    info.pCode    = spirv.data();

    VkShaderModule mod = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device, &info, nullptr, &mod));
    return mod;
}

VkDescriptorSetLayout createSetLayout(VkDevice device,
                                      const std::vector<VkDescriptorSetLayoutBinding> &bindings) {
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<std::uint32_t>(bindings.size());
    info.pBindings    = bindings.empty() ? nullptr : bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &layout));
    return layout;
}

} // namespace

VkShader::VkShader(Device &device,
                   std::string vertexSource, std::string fragmentSource,
                   std::vector<std::uint32_t> vertexSpirv,
                   std::vector<std::uint32_t> fragmentSpirv,
                   ShaderReflection reflection)
    : m_device(device),
      m_vertexSource(std::move(vertexSource)),
      m_fragmentSource(std::move(fragmentSource)),
      m_vertSpirv(std::move(vertexSpirv)),
      m_fragSpirv(std::move(fragmentSpirv)),
      m_reflection(std::move(reflection)) {
    VkDevice d = device.logical();
    m_vertModule = createModule(d, m_vertSpirv);
    m_fragModule = createModule(d, m_fragSpirv);

    // Create one VkDescriptorSetLayout per set. Empty sets still produce a
    // valid VkDescriptorSetLayout so pipeline layout sees all expected set
    // indices.
    m_setLayouts.reserve(m_reflection.setBindings.size());
    for (const auto &bindings : m_reflection.setBindings) {
        m_setLayouts.push_back(createSetLayout(d, bindings));
    }

    VkPipelineLayoutCreateInfo info{};
    info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount         = static_cast<std::uint32_t>(m_setLayouts.size());
    info.pSetLayouts            = m_setLayouts.empty() ? nullptr : m_setLayouts.data();
    info.pushConstantRangeCount = static_cast<std::uint32_t>(m_reflection.pushConstantRanges.size());
    info.pPushConstantRanges    = m_reflection.pushConstantRanges.empty()
                                  ? nullptr : m_reflection.pushConstantRanges.data();
    VK_CHECK(vkCreatePipelineLayout(d, &info, nullptr, &m_pipelineLayout));
}

VkShader::~VkShader() {
    VkDevice d = m_device.logical();
    if (m_pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(d, m_pipelineLayout, nullptr);
    for (auto layout : m_setLayouts) {
        if (layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(d, layout, nullptr);
    }
    if (m_vertModule != VK_NULL_HANDLE) vkDestroyShaderModule(d, m_vertModule, nullptr);
    if (m_fragModule != VK_NULL_HANDLE) vkDestroyShaderModule(d, m_fragModule, nullptr);
}

void VkShader::bind()   const {}
void VkShader::unbind() const {}

} // namespace sonnet::renderer::vulkan
