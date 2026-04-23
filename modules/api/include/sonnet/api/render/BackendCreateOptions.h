#pragma once

namespace sonnet::api::render {

// Options passed to a renderer backend at construction time. Fields that
// don't apply to a given backend (e.g. Vulkan validation layers under OpenGL)
// are silently ignored.
struct BackendCreateOptions {
    bool enableValidation = false; // Vulkan-only.
    bool vsync            = true;
};

} // namespace sonnet::api::render
