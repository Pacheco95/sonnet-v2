#include "VkSamplerCache.h"

#include "VkDevice.h"
#include "VkFormatMap.h"
#include "VkUtils.h"

#include <cstring>

namespace sonnet::renderer::vulkan {

std::size_t SamplerCache::Hasher::operator()(const api::render::SamplerDesc &d) const noexcept {
    std::size_t h = 0;
    // FNV-1a on raw bytes is fine here — the struct is POD-ish with no padding
    // after the bool (alignof == 1) and gives a stable value.
    const auto *p = reinterpret_cast<const std::uint8_t *>(&d);
    for (std::size_t i = 0; i < sizeof(d); ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

SamplerCache::SamplerCache(Device &device) : m_device(device) {}

SamplerCache::~SamplerCache() {
    for (auto &[desc, sampler] : m_samplers) {
        vkDestroySampler(m_device.logical(), sampler, nullptr);
    }
}

VkSampler SamplerCache::get(const api::render::SamplerDesc &desc) {
    if (auto it = m_samplers.find(desc); it != m_samplers.end()) {
        return it->second;
    }

    const auto min = toVkMinFilter(desc.minFilter);

    VkSamplerCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter    = toVkMagFilter(desc.magFilter);
    info.minFilter    = min.filter;
    info.mipmapMode   = min.mipmap;
    info.addressModeU = toVkAddressMode(desc.wrapS);
    info.addressModeV = toVkAddressMode(desc.wrapT);
    info.addressModeW = toVkAddressMode(desc.wrapR);
    info.mipLodBias   = 0.0f;
    info.anisotropyEnable = VK_FALSE;
    info.maxAnisotropy    = 1.0f;
    info.compareEnable    = desc.depthCompare ? VK_TRUE : VK_FALSE;
    info.compareOp        = VK_COMPARE_OP_LESS_OR_EQUAL;
    info.minLod           = 0.0f;
    info.maxLod           = VK_LOD_CLAMP_NONE;
    info.borderColor      = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    info.unnormalizedCoordinates = VK_FALSE;

    VkSampler sampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(m_device.logical(), &info, nullptr, &sampler));
    m_samplers.emplace(desc, sampler);
    return sampler;
}

} // namespace sonnet::renderer::vulkan
