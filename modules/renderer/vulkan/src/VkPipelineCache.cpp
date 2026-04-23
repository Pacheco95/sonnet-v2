#include "VkPipelineCache.h"

#include <sonnet/renderer/vulkan/VkShader.h>
#include <sonnet/renderer/vulkan/VkVertexInputState.h>

#include "VkDevice.h"
#include "VkUtils.h"

#include <array>

namespace sonnet::renderer::vulkan {

namespace {

VkCompareOp toVkCompareOp(api::render::DepthFunction f) {
    switch (f) {
        case api::render::DepthFunction::Less:      return VK_COMPARE_OP_LESS;
        case api::render::DepthFunction::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    }
    return VK_COMPARE_OP_LESS;
}

VkCullModeFlags toVkCullMode(api::render::CullMode c) {
    switch (c) {
        case api::render::CullMode::None:  return VK_CULL_MODE_NONE;
        case api::render::CullMode::Back:  return VK_CULL_MODE_BACK_BIT;
        case api::render::CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    }
    return VK_CULL_MODE_BACK_BIT;
}

VkPolygonMode toVkPolygonMode(api::render::FillMode f) {
    switch (f) {
        case api::render::FillMode::Solid:     return VK_POLYGON_MODE_FILL;
        case api::render::FillMode::Wireframe: return VK_POLYGON_MODE_LINE;
    }
    return VK_POLYGON_MODE_FILL;
}

struct BlendSetup { VkBlendFactor src; VkBlendFactor dst; bool enable; };

BlendSetup blendFor(api::render::BlendMode mode) {
    switch (mode) {
        case api::render::BlendMode::Opaque:
            return {VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, false};
        case api::render::BlendMode::Alpha:
            return {VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, true};
        case api::render::BlendMode::Additive:
            return {VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, true};
    }
    return {VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, false};
}

} // namespace

std::size_t PipelineCache::Hasher::operator()(const Key &k) const noexcept {
    std::size_t h = 0;
    auto combine = [&](std::size_t x) { h ^= x + 0x9e3779b9 + (h << 6) + (h >> 2); };
    combine(reinterpret_cast<std::uintptr_t>(k.shader));
    combine(reinterpret_cast<std::uintptr_t>(k.vis));
    combine(reinterpret_cast<std::uintptr_t>(k.renderPass));
    combine(api::render::RenderState::Hasher{}(k.state));
    combine(k.colorAttachmentCount);
    combine(static_cast<std::size_t>(k.hasDepth));
    return h;
}

PipelineCache::PipelineCache(Device &device) : m_device(device) {
    VkPipelineCacheCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    // Empty initial data; disk persistence deferred to Phase 8.
    VK_CHECK(vkCreatePipelineCache(device.logical(), &info, nullptr, &m_vkCache));
}

PipelineCache::~PipelineCache() {
    for (auto &[_, pipe] : m_map) {
        if (pipe != VK_NULL_HANDLE) vkDestroyPipeline(m_device.logical(), pipe, nullptr);
    }
    if (m_vkCache != VK_NULL_HANDLE) vkDestroyPipelineCache(m_device.logical(), m_vkCache, nullptr);
}

VkPipeline PipelineCache::getOrCreate(const VkShader &shader,
                                      const api::render::RenderState &state,
                                      const VkVertexInputState &vis,
                                      VkRenderPass renderPass,
                                      std::uint32_t colorAttachmentCount,
                                      bool hasDepthAttachment) {
    Key key{
        .shader               = &shader,
        .vis                  = &vis,
        .state                = state,
        .renderPass           = renderPass,
        .colorAttachmentCount = colorAttachmentCount,
        .hasDepth             = hasDepthAttachment,
    };
    if (auto it = m_map.find(key); it != m_map.end()) return it->second;
    VkPipeline p = build(key);
    m_map.emplace(key, p);
    return p;
}

VkPipeline PipelineCache::build(const Key &k) {
    // Shader stages.
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = k.shader->vertexModule();
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = k.shader->fragmentModule();
    stages[1].pName  = "main";

    // Vertex input.
    const auto &binding    = k.vis->bindingDescription();
    const auto &attributes = k.vis->attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = attributes.empty() ? 0 : 1;
    vertexInput.pVertexBindingDescriptions      = attributes.empty() ? nullptr : &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions    = attributes.empty() ? nullptr : attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport/scissor are dynamic; the state carries one-of-each but the
    // actual values come from vkCmdSetViewport/Scissor.
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = toVkPolygonMode(k.state.fill);
    raster.cullMode    = toVkCullMode(k.state.cull);
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable       = k.state.depthTest  ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable      = k.state.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp        = toVkCompareOp(k.state.depthFunc);
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable     = VK_FALSE;

    // Per-attachment blend state (all attachments share the same setup here).
    const auto blendSetup = blendFor(k.state.blend);
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(k.colorAttachmentCount);
    for (auto &ba : blendAttachments) {
        ba.blendEnable         = blendSetup.enable ? VK_TRUE : VK_FALSE;
        ba.srcColorBlendFactor = blendSetup.src;
        ba.dstColorBlendFactor = blendSetup.dst;
        ba.colorBlendOp        = VK_BLEND_OP_ADD;
        ba.srcAlphaBlendFactor = blendSetup.src;
        ba.dstAlphaBlendFactor = blendSetup.dst;
        ba.alphaBlendOp        = VK_BLEND_OP_ADD;
        ba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                               | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = static_cast<std::uint32_t>(blendAttachments.size());
    colorBlend.pAttachments    = blendAttachments.empty() ? nullptr : blendAttachments.data();

    const VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dynStates;

    VkGraphicsPipelineCreateInfo info{};
    info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount          = 2;
    info.pStages             = stages.data();
    info.pVertexInputState   = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState      = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState   = &multisample;
    info.pDepthStencilState  = k.hasDepth ? &depthStencil : nullptr;
    info.pColorBlendState    = &colorBlend;
    info.pDynamicState       = &dynamic;
    info.layout              = k.shader->pipelineLayout();
    info.renderPass          = k.renderPass;
    info.subpass             = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(m_device.logical(), m_vkCache,
                                        1, &info, nullptr, &pipeline));
    return pipeline;
}

} // namespace sonnet::renderer::vulkan
