#pragma once

#include <filesystem>
#include <string>

namespace sonnet::loaders {

// Reads a GLSL shader source file and returns its content as a string.
class ShaderLoader {
public:
    [[nodiscard]] static std::string load(const std::filesystem::path &path);
};

} // namespace sonnet::loaders
