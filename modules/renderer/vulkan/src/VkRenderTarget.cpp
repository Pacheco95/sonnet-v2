#include <sonnet/renderer/vulkan/VkRenderTarget.h>

#include "VkDevice.h"
#include "VkFormatMap.h"
#include "VkSamplerCache.h"
#include "VkUtils.h"

namespace sonnet::renderer::vulkan {

namespace {

api::render::TextureDesc makeAttachmentDesc(const api::render::TextureAttachmentDesc &src,
                                            std::uint32_t width, std::uint32_t height,
                                            api::render::TextureUsage attachmentUsage) {
    api::render::TextureDesc d{};
    d.size       = {width, height};
    d.format     = src.format;
    d.type       = api::render::TextureType::Texture2D;
    d.usageFlags = static_cast<api::render::TextureUsage>(
        static_cast<std::uint8_t>(api::render::Sampled) |
        static_cast<std::uint8_t>(attachmentUsage));
    d.colorSpace = api::render::ColorSpace::Linear;
    d.useMipmaps = false;
    return d;
}

} // namespace

VkRenderTarget::VkRenderTarget(Device &device, SamplerCache &samplers,
                               const api::render::RenderTargetDesc &desc)
    : m_device(device), m_width(desc.width), m_height(desc.height) {
    // 1. Color attachments.
    m_colors.reserve(desc.colors.size());
    for (const auto &c : desc.colors) {
        const auto td = makeAttachmentDesc(c, desc.width, desc.height,
                                           api::render::ColorAttachment);
        m_colors.emplace_back(std::make_unique<VkTexture2D>(device, samplers, td, c.samplerDesc));
    }

    // 2. Depth attachment (TextureAttachmentDesc only — RenderBufferDesc not used
    //    by the current demo; a future phase can add it via VkImage without view).
    if (desc.depth) {
        if (const auto *td = std::get_if<api::render::TextureAttachmentDesc>(&*desc.depth)) {
            const auto dd = makeAttachmentDesc(*td, desc.width, desc.height,
                                               api::render::DepthAttachment);
            m_depth = std::make_unique<VkTexture2D>(device, samplers, dd, td->samplerDesc);
        } else {
            throw VulkanError("VkRenderTarget: RenderBufferDesc depth not implemented "
                              "(use TextureAttachmentDesc for the depth attachment).");
        }
    }

    buildRenderPass();
    buildFramebuffer();
}

VkRenderTarget::~VkRenderTarget() {
    VkDevice d = m_device.logical();
    if (m_framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(d, m_framebuffer, nullptr);
    if (m_renderPass  != VK_NULL_HANDLE) vkDestroyRenderPass(d, m_renderPass, nullptr);
}

void VkRenderTarget::bind() const {
    // Active-RT tracking happens in VkRendererBackend::bindRenderTarget.
}

void VkRenderTarget::buildRenderPass() {
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference>   colorRefs;
    VkAttachmentReference                depthRef{};
    bool                                 hasDepth = false;

    attachments.reserve(m_colors.size() + 1);
    colorRefs.reserve(m_colors.size());

    for (const auto &c : m_colors) {
        VkAttachmentDescription a{};
        a.format         = c->format();
        a.samples        = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        a.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorRefs.push_back({static_cast<std::uint32_t>(attachments.size()),
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        attachments.push_back(a);
    }

    if (m_depth) {
        VkAttachmentDescription a{};
        a.format         = m_depth->format();
        a.samples        = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        a.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthRef = {static_cast<std::uint32_t>(attachments.size()),
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        attachments.push_back(a);
        hasDepth = true;
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = static_cast<std::uint32_t>(colorRefs.size());
    subpass.pColorAttachments       = colorRefs.data();
    subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

    // Two dependencies: external → subpass to prepare attachments for write,
    // subpass → external so downstream passes can sample the color outputs.
    VkSubpassDependency depIn{};
    depIn.srcSubpass    = VK_SUBPASS_EXTERNAL;
    depIn.dstSubpass    = 0;
    depIn.srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    depIn.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                        | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    depIn.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    depIn.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                        | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency depOut{};
    depOut.srcSubpass    = 0;
    depOut.dstSubpass    = VK_SUBPASS_EXTERNAL;
    depOut.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    depOut.dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    depOut.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    depOut.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    const VkSubpassDependency deps[] = {depIn, depOut};

    VkRenderPassCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    info.pAttachments    = attachments.data();
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = 2;
    info.pDependencies   = deps;
    VK_CHECK(vkCreateRenderPass(m_device.logical(), &info, nullptr, &m_renderPass));
}

void VkRenderTarget::buildFramebuffer() {
    std::vector<VkImageView> views;
    views.reserve(m_colors.size() + 1);
    for (const auto &c : m_colors) views.push_back(c->imageView());
    if (m_depth) views.push_back(m_depth->imageView());

    VkFramebufferCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass      = m_renderPass;
    info.attachmentCount = static_cast<std::uint32_t>(views.size());
    info.pAttachments    = views.data();
    info.width           = m_width;
    info.height          = m_height;
    info.layers          = 1;
    VK_CHECK(vkCreateFramebuffer(m_device.logical(), &info, nullptr, &m_framebuffer));
}

} // namespace sonnet::renderer::vulkan
