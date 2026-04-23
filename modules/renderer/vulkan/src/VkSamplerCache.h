#pragma once

#include <sonnet/api/render/ITexture.h>

#include <vulkan/vulkan.h>

#include <unordered_map>

namespace sonnet::renderer::vulkan {

class Device;

// Caches VkSamplers keyed on SamplerDesc. Owned by VkRendererBackend and
// shared across all VkTexture2Ds. Samplers live as long as the cache.
class SamplerCache {
public:
    explicit SamplerCache(Device &device);
    ~SamplerCache();

    SamplerCache(const SamplerCache &)            = delete;
    SamplerCache &operator=(const SamplerCache &) = delete;

    VkSampler get(const api::render::SamplerDesc &desc);

private:
    struct Hasher {
        std::size_t operator()(const api::render::SamplerDesc &d) const noexcept;
    };

    Device &m_device;
    std::unordered_map<api::render::SamplerDesc, VkSampler, Hasher> m_samplers;
};

} // namespace sonnet::renderer::vulkan
