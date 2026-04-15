#include "ShaderRegistry.h"

#include <sonnet/loaders/ShaderLoader.h>

#include <filesystem>

namespace fs = std::filesystem;

ShaderRegistry::ShaderRegistry(sonnet::renderer::frontend::Renderer &renderer)
    : m_renderer(renderer) {}

sonnet::core::ShaderHandle ShaderRegistry::compile(std::string_view vertPath,
                                                    std::string_view fragPath) {
    const auto vert   = sonnet::loaders::ShaderLoader::load(vertPath);
    const auto frag   = sonnet::loaders::ShaderLoader::load(fragPath);
    const auto handle = m_renderer.createShader(vert, frag);
    m_shaders.push_back({
        handle,
        std::string(vertPath),
        std::string(fragPath),
        fs::last_write_time(vertPath),
        fs::last_write_time(fragPath),
    });
    return handle;
}

std::string ShaderRegistry::tick(double dt) {
    m_pollAccum += dt;
    if (m_pollAccum < 0.5) return {};
    m_pollAccum = 0.0;

    for (auto &rec : m_shaders) {
        try {
            const auto vmt = fs::last_write_time(rec.vertPath);
            const auto fmt = fs::last_write_time(rec.fragPath);
            if (vmt == rec.vertMtime && fmt == rec.fragMtime) continue;
            rec.vertMtime = vmt;
            rec.fragMtime = fmt;
            const auto vert = sonnet::loaders::ShaderLoader::load(rec.vertPath);
            const auto frag = sonnet::loaders::ShaderLoader::load(rec.fragPath);
            m_renderer.reloadShader(rec.handle, vert, frag);
            return "Reloaded: " + fs::path(rec.fragPath).filename().string();
        } catch (const std::exception &e) {
            return "Error (" + fs::path(rec.fragPath).filename().string() + "): " + e.what();
        }
    }
    return {};
}
