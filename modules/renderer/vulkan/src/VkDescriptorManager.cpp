#include "VkDescriptorManager.h"

#include <sonnet/renderer/vulkan/VkShader.h>
#include <sonnet/renderer/vulkan/VkTexture2D.h>

#include "VkBindState.h"
#include "VkDevice.h"
#include "VkUtils.h"

#include <array>
#include <vector>

namespace sonnet::renderer::vulkan {

namespace {

// Per-frame pool capacity. Sized to comfortably cover every draw-time set
// allocation in a frame without needing auto-grow logic in v1. Increase
// if drawIndexed ever returns OUT_OF_POOL_MEMORY.
constexpr std::uint32_t kMaxSetsPerFrame                  = 512;
constexpr std::uint32_t kMaxUniformBuffersPerFrame        = 1024;
constexpr std::uint32_t kMaxCombinedImageSamplersPerFrame = 4096;

} // namespace

DescriptorManager::DescriptorManager(Device &device, BindState &bindState)
    : m_device(device), m_bindState(bindState) {
    const std::array<VkDescriptorPoolSize, 2> sizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         kMaxUniformBuffersPerFrame},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxCombinedImageSamplersPerFrame},
    };

    VkDescriptorPoolCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets       = kMaxSetsPerFrame;
    info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    info.pPoolSizes    = sizes.data();

    for (auto &pool : m_pools) {
        VK_CHECK(vkCreateDescriptorPool(device.logical(), &info, nullptr, &pool));
    }
}

DescriptorManager::~DescriptorManager() {
    for (auto &pool : m_pools) {
        if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_device.logical(), pool, nullptr);
    }
}

void DescriptorManager::beginFrame(std::uint32_t frameIx) {
    m_currentFrame = frameIx % kFramesInFlight;
    vkResetDescriptorPool(m_device.logical(), m_pools[m_currentFrame], 0);
}

VkDescriptorSet DescriptorManager::allocateFrameSet0(const VkShader &shader) {
    const auto &layouts = shader.setLayouts();
    if (layouts.empty()) return VK_NULL_HANDLE;

    const auto &reflection = shader.reflection();
    if (reflection.setBindings.empty() || reflection.setBindings[0].empty()) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool     = m_pools[m_currentFrame];
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts        = &layouts[0];

    VkDescriptorSet set = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateDescriptorSets(m_device.logical(), &alloc, &set));

    // Populate each UBO binding from the frame's BindState.ubos[] table.
    // Skip bindings that haven't been bound; Vulkan needs every binding in
    // the layout to be valid at draw time, so if the engine omits a UBO it's
    // the engine's responsibility to ensure the shader doesn't reference it.
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkWriteDescriptorSet>   writes;
    bufferInfos.reserve(reflection.setBindings[0].size());
    writes.reserve(reflection.setBindings[0].size());

    for (const auto &b : reflection.setBindings[0]) {
        if (b.descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) continue;
        if (b.binding >= BindState::kMaxUboBindings) continue;
        if (m_bindState.ubos[b.binding] == VK_NULL_HANDLE)         continue;

        VkDescriptorBufferInfo info{};
        info.buffer = m_bindState.ubos[b.binding];
        info.offset = 0;
        info.range  = m_bindState.uboSizes[b.binding];
        bufferInfos.push_back(info);

        VkWriteDescriptorSet w{};
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = set;
        w.dstBinding      = b.binding;
        w.descriptorCount = 1;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo     = &bufferInfos.back();
        writes.push_back(w);
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(m_device.logical(),
                               static_cast<std::uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }

    return set;
}

VkDescriptorSet DescriptorManager::allocateMaterialSet1(const VkShader &shader) {
    const auto &layouts = shader.setLayouts();
    if (layouts.size() < 2) return VK_NULL_HANDLE;

    const auto &reflection = shader.reflection();
    if (reflection.setBindings.size() < 2 || reflection.setBindings[1].empty()) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool     = m_pools[m_currentFrame];
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts        = &layouts[1];

    VkDescriptorSet set = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateDescriptorSets(m_device.logical(), &alloc, &set));

    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkWriteDescriptorSet>  writes;
    imageInfos.reserve(reflection.setBindings[1].size());
    writes.reserve(reflection.setBindings[1].size());

    for (const auto &b : reflection.setBindings[1]) {
        if (b.descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
            b.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) continue;
        if (b.binding >= BindState::kMaxMaterialTextures) continue;

        const auto *tex = m_bindState.materialTextures[b.binding];
        if (!tex) continue;

        VkDescriptorImageInfo ii{};
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ii.imageView   = tex->imageView();
        ii.sampler     = tex->sampler();
        imageInfos.push_back(ii);

        VkWriteDescriptorSet w{};
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = set;
        w.dstBinding      = b.binding;
        w.descriptorCount = 1;
        w.descriptorType  = b.descriptorType;
        w.pImageInfo      = &imageInfos.back();
        writes.push_back(w);
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(m_device.logical(),
                               static_cast<std::uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }

    return set;
}

} // namespace sonnet::renderer::vulkan
