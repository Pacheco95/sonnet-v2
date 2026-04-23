// Sonnet v2 — Vulkan demo (Phase 1)
//
// Minimal loop that exercises VkRendererBackend's initialize / beginFrame /
// clear / endFrame pipeline. No resources, no Renderer, no ImGui yet —
// those come online in phases 2-4. The window renders a solid dark-gray
// clear that responds to resizes.

#include <sonnet/input/InputSystem.h>
#include <sonnet/renderer/frontend/BackendFactory.h>
#include <sonnet/window/GLFWInputAdapter.h>
#include <sonnet/window/GLFWWindow.h>

int main() {
    sonnet::window::GLFWWindow      window{{1280, 720, "Sonnet v2 Demo (Vulkan)"}};
    sonnet::input::InputSystem      input;
    sonnet::window::GLFWInputAdapter adapter{input};
    window.setInputAdapter(&adapter);

    auto backend = sonnet::renderer::frontend::makeBackend(window);
    backend->initialize();

    while (!window.shouldClose()) {
        window.pollEvents();

        if (input.isKeyJustPressed(sonnet::api::input::Key::Escape)) {
            window.requestClose();
        }

        const auto fb = window.getFrameBufferSize();
        backend->beginFrame();
        backend->setViewport(fb.x, fb.y);
        backend->endFrame();

        window.swapBuffers(); // no-op under Vulkan; harmless.
        input.nextFrame();
    }

    return 0;
}
