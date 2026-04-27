#include <sonnet/loaders/TextureLoader.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

using sonnet::loaders::TextureLoader;
using sonnet::loaders::TextureLoadOptions;

namespace {

namespace fs = std::filesystem;

fs::path makeTempDir() {
    static std::atomic<unsigned> counter{0};
    auto base = fs::temp_directory_path() / "sonnet_texture_loader_tests";
    fs::create_directories(base);
    auto dir = base / ("run_" + std::to_string(::getpid()) + "_" +
                       std::to_string(counter.fetch_add(1)));
    fs::create_directories(dir);
    return dir;
}

// Write a 2-row × 1-col P6 PPM whose top row is red and bottom row is blue.
// stb_image supports PPM/PGM out of the box, so we avoid pulling in a PNG
// encoder just for fixtures.
fs::path writeRedBluePpm(const fs::path &dir) {
    auto path = dir / "red_blue.ppm";
    std::ofstream out{path, std::ios::binary};
    out << "P6\n1 2\n255\n";
    const unsigned char pixels[] = {
        255, 0, 0,    // row 0: red
        0,   0, 255,  // row 1: blue
    };
    out.write(reinterpret_cast<const char *>(pixels), sizeof(pixels));
    return path;
}

} // namespace

TEST_CASE("TextureLoader::load reads PPM dimensions and channel count",
         "[loaders][texture]") {
    auto dir  = makeTempDir();
    auto path = writeRedBluePpm(dir);

    auto buf = TextureLoader::load(path);
    REQUIRE(buf.width    == 1u);
    REQUIRE(buf.height   == 2u);
    REQUIRE(buf.channels == 3u);
    REQUIRE(buf.texels.size() == 1u * 2u * 3u);
}

TEST_CASE("TextureLoader::load with flipVertically=false preserves source row order",
         "[loaders][texture][flip]") {
    auto dir  = makeTempDir();
    auto path = writeRedBluePpm(dir);

    auto buf = TextureLoader::load(path, TextureLoadOptions{.flipVertically = false});
    REQUIRE(buf.texels.size() >= 6);
    // First row in source order is red; last row is blue.
    REQUIRE(static_cast<unsigned>(buf.texels[0]) == 255u);
    REQUIRE(static_cast<unsigned>(buf.texels[1]) == 0u);
    REQUIRE(static_cast<unsigned>(buf.texels[2]) == 0u);
    REQUIRE(static_cast<unsigned>(buf.texels[5]) == 255u); // blue B channel
}

TEST_CASE("TextureLoader::load with flipVertically=true swaps row order",
         "[loaders][texture][flip]") {
    auto dir  = makeTempDir();
    auto path = writeRedBluePpm(dir);

    auto buf = TextureLoader::load(path, TextureLoadOptions{.flipVertically = true});
    REQUIRE(buf.texels.size() >= 6);
    // After vertical flip, the first row is the *bottom* of the source (blue).
    REQUIRE(static_cast<unsigned>(buf.texels[2]) == 255u); // blue B channel first
    REQUIRE(static_cast<unsigned>(buf.texels[3]) == 255u); // red R channel second
}

TEST_CASE("TextureLoader::load on a missing file throws runtime_error",
         "[loaders][texture][error]") {
    try {
        (void)TextureLoader::load("/tmp/sonnet_texture_loader_tests/__nope__.ppm");
        FAIL("expected runtime_error");
    } catch (const std::runtime_error &e) {
        std::string what = e.what();
        REQUIRE(what.find("TextureLoader") != std::string::npos);
    }
}

TEST_CASE("TextureLoader::load on a non-image file throws",
         "[loaders][texture][error]") {
    auto dir  = makeTempDir();
    auto path = dir / "garbage.bin";
    {
        std::ofstream out{path, std::ios::binary};
        out << "this is not an image";
    }

    REQUIRE_THROWS_AS(TextureLoader::load(path), std::runtime_error);
}
