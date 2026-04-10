#include <sonnet/loaders/ShaderLoader.h>

#include <fstream>
#include <stdexcept>

namespace sonnet::loaders {

std::string ShaderLoader::load(const std::filesystem::path &path) {
    std::ifstream f{path};
    if (!f)
        throw std::runtime_error("ShaderLoader: cannot open '" + path.string() + "'");
    return {std::istreambuf_iterator<char>{f}, std::istreambuf_iterator<char>{}};
}

} // namespace sonnet::loaders
