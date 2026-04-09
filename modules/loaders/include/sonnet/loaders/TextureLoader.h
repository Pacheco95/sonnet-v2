#pragma once

#include <sonnet/api/render/ITexture.h>

#include <filesystem>

namespace sonnet::loaders {

struct TextureLoadOptions {
    bool flipVertically = true;  // flip Y so (0,0) is bottom-left for OpenGL
};

class TextureLoader {
public:
    [[nodiscard]] static api::render::CPUTextureBuffer load(
        const std::filesystem::path &path,
        const TextureLoadOptions &options = {});
};

} // namespace sonnet::loaders
