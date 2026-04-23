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

} // namespace

VkShader::VkShader(Device &device,
                   std::string vertexSource, std::string fragmentSource,
                   std::vector<std::uint32_t> vertexSpirv,
                   std::vector<std::uint32_t> fragmentSpirv)
    : m_device(device),
      m_vertexSource(std::move(vertexSource)),
      m_fragmentSource(std::move(fragmentSource)),
      m_vertSpirv(std::move(vertexSpirv)),
      m_fragSpirv(std::move(fragmentSpirv)) {
    m_vertModule = createModule(device.logical(), m_vertSpirv);
    m_fragModule = createModule(device.logical(), m_fragSpirv);
}

VkShader::~VkShader() {
    VkDevice d = m_device.logical();
    if (m_vertModule != VK_NULL_HANDLE) vkDestroyShaderModule(d, m_vertModule, nullptr);
    if (m_fragModule != VK_NULL_HANDLE) vkDestroyShaderModule(d, m_fragModule, nullptr);
}

void VkShader::bind()   const {}
void VkShader::unbind() const {}

} // namespace sonnet::renderer::vulkan
