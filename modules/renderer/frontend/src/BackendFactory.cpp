#include <sonnet/renderer/frontend/BackendFactory.h>

#if defined(SONNET_USE_OPENGL)
#  include <sonnet/renderer/opengl/GlRendererBackend.h>
#endif
#if defined(SONNET_USE_VULKAN)
#  include <sonnet/renderer/vulkan/VkRendererBackend.h>
#endif

namespace sonnet::renderer::frontend {

std::unique_ptr<api::render::IRendererBackend>
makeBackend([[maybe_unused]] api::window::IWindow &window,
            [[maybe_unused]] const BackendCreateOptions &opts) {
#if defined(SONNET_USE_VULKAN)
    return std::make_unique<vulkan::VkRendererBackend>(window, opts);
#elif defined(SONNET_USE_OPENGL)
    return std::make_unique<opengl::GlRendererBackend>();
#else
#   error "No renderer backend enabled. Configure with -DSONNET_RENDERER_BACKEND=OpenGL|Vulkan|Auto."
#endif
}

} // namespace sonnet::renderer::frontend
