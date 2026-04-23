#pragma once

#include <sonnet/api/render/BackendCreateOptions.h>
#include <sonnet/api/render/IRendererBackend.h>
#include <sonnet/api/window/IWindow.h>

#include <memory>

namespace sonnet::renderer::frontend {

// Re-exported for callers that spell it frontend::BackendCreateOptions.
using BackendCreateOptions = api::render::BackendCreateOptions;

// Instantiate the renderer backend selected at CMake configuration time.
// The returned backend is uninitialized; the caller must invoke initialize().
[[nodiscard]] std::unique_ptr<api::render::IRendererBackend>
makeBackend(api::window::IWindow &window,
            const BackendCreateOptions &opts = {});

} // namespace sonnet::renderer::frontend
