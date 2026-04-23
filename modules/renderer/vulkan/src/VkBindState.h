#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace sonnet::renderer::vulkan {

class VkShader;
class VkVertexInputState;
class VkTexture2D;

// Mutable state the backend's drawIndexed() reads to assemble a draw:
// bound buffers, active shader, active vertex input, UBO table (set=0),
// material texture table (set=1), and push-constant staging (set by
// setUniform). Owned by VkRendererBackend; passed by reference to the
// objects that mutate it. This is Vulkan's emulation of OpenGL's context
// binding model.
struct BindState {
    static constexpr std::uint32_t kMaxUboBindings      = 8;
    static constexpr std::uint32_t kMaxMaterialTextures = 16;
    static constexpr std::uint32_t kPushConstantBytes   = 128;
    static constexpr std::uint32_t kPerDrawStagingBytes = 1024;

    VkBuffer                              currentVertex = VK_NULL_HANDLE;
    VkBuffer                              currentIndex  = VK_NULL_HANDLE;
    VkIndexType                           indexType     = VK_INDEX_TYPE_UINT32;
    std::array<VkBuffer, kMaxUboBindings> ubos{};
    std::array<VkDeviceSize, kMaxUboBindings> uboSizes{};

    const VkShader           *currentShader      = nullptr;
    const VkVertexInputState *currentVertexInput = nullptr;

    // Material textures, indexed by slot (the value passed to texture->bind
    // by Renderer::bindMaterial, which equals the descriptor binding in set=1).
    std::array<const VkTexture2D *, kMaxMaterialTextures> materialTextures{};

    // Staged push-constant bytes + hwm of the last byte written this draw.
    std::array<std::uint8_t, kPushConstantBytes> pushConstantStaging{};
    std::uint32_t                                pushConstantDirtyEnd = 0;

    // Staged PerDraw UBO bytes + hwm. Flushed to the uniform ring in drawIndexed
    // when the shader declares a set=2 UBO.
    std::array<std::uint8_t, kPerDrawStagingBytes> perDrawStaging{};
    std::uint32_t                                  perDrawDirtyEnd = 0;

    void reset() {
        currentVertex      = VK_NULL_HANDLE;
        currentIndex       = VK_NULL_HANDLE;
        currentShader      = nullptr;
        currentVertexInput = nullptr;
        ubos.fill(VK_NULL_HANDLE);
        uboSizes.fill(0);
        materialTextures.fill(nullptr);
        pushConstantDirtyEnd = 0;
        perDrawDirtyEnd      = 0;
    }

    // Call after the draw consumes pending per-draw state; keeps the
    // shader+buffer bindings so subsequent draws from the same material
    // don't need to re-record them.
    void clearDrawScopedState() {
        materialTextures.fill(nullptr);
        pushConstantDirtyEnd = 0;
        perDrawDirtyEnd      = 0;
    }
};

} // namespace sonnet::renderer::vulkan
