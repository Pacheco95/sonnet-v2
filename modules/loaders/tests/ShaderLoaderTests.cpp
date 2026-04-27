#include <sonnet/loaders/ShaderLoader.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

using sonnet::loaders::ShaderLoader;

namespace {

namespace fs = std::filesystem;

fs::path makeTempDir() {
    static std::atomic<unsigned> counter{0};
    auto base = fs::temp_directory_path() / "sonnet_shader_loader_tests";
    fs::create_directories(base);
    auto dir = base / ("run_" + std::to_string(::getpid()) + "_" +
                       std::to_string(counter.fetch_add(1)));
    fs::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("ShaderLoader::load returns full file contents byte-for-byte",
         "[loaders][shader]") {
    auto dir = makeTempDir();
    auto path = dir / "frag.glsl";

    const std::string body =
        "#version 450 core\n"
        "// embedded // and \"quotes\" should round-trip\n"
        "layout(location = 0) out vec4 oColor;\n"
        "void main() { oColor = vec4(1.0); }\n";

    {
        std::ofstream out{path};
        out << body;
    }

    REQUIRE(ShaderLoader::load(path) == body);
}

TEST_CASE("ShaderLoader::load preserves trailing data and embedded NUL-free binary bytes",
         "[loaders][shader]") {
    auto dir = makeTempDir();
    auto path = dir / "weird.glsl";

    {
        std::ofstream out{path, std::ios::binary};
        out << "header\r\nline2\n\nfinal";
    }

    auto loaded = ShaderLoader::load(path);
    // The loader does not normalise line endings — verify the CR survives.
    REQUIRE(loaded.find('\r') != std::string::npos);
    REQUIRE(loaded.size() == fs::file_size(path));
}

TEST_CASE("ShaderLoader::load on a missing path throws runtime_error with the path",
         "[loaders][shader][error]") {
    const fs::path missing = "/tmp/sonnet_shader_loader_tests/__nope__.glsl";
    try {
        (void)ShaderLoader::load(missing);
        FAIL("expected runtime_error");
    } catch (const std::runtime_error &e) {
        std::string what = e.what();
        REQUIRE(what.find("ShaderLoader") != std::string::npos);
        REQUIRE(what.find(missing.string()) != std::string::npos);
    }
}

TEST_CASE("ShaderLoader::load on an empty file returns empty string",
         "[loaders][shader]") {
    auto dir = makeTempDir();
    auto path = dir / "empty.glsl";
    std::ofstream{path};

    REQUIRE(ShaderLoader::load(path).empty());
}
