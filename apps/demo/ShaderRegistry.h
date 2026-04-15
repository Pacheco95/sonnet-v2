#pragma once

#include <sonnet/renderer/frontend/Renderer.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct ShaderRecord {
    sonnet::core::ShaderHandle      handle;
    std::string                     vertPath;
    std::string                     fragPath;
    std::filesystem::file_time_type vertMtime;
    std::filesystem::file_time_type fragMtime;
};

// Wraps shader compilation and hot-reload polling.
// All shaders compiled via compile() are polled every ~0.5 s.
class ShaderRegistry {
public:
    explicit ShaderRegistry(sonnet::renderer::frontend::Renderer &renderer);

    // Compile shader from files, register for hot-reload, return handle.
    [[nodiscard]] sonnet::core::ShaderHandle compile(std::string_view vert,
                                                     std::string_view frag);

    // Poll for shader file changes (called once per frame with frame dt in seconds).
    // Returns a human-readable reload notification string, or "" if nothing changed.
    std::string tick(double dt);

private:
    sonnet::renderer::frontend::Renderer &m_renderer;
    std::vector<ShaderRecord>             m_shaders;
    double                                m_pollAccum = 0.0;
};
