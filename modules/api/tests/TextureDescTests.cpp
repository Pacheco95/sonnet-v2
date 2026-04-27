#include <catch2/catch_test_macros.hpp>

#include <sonnet/api/render/ITexture.h>

using namespace sonnet::api::render;

TEST_CASE("TextureDesc: defaults match documented values", "[texturedesc]") {
    TextureDesc d{};
    REQUIRE(d.size       == glm::uvec2{1, 1});
    REQUIRE(d.format     == TextureFormat::RGBA8);
    REQUIRE(d.type       == TextureType::Texture2D);
    REQUIRE(d.usageFlags == TextureUsage::Sampled);
    REQUIRE(d.colorSpace == ColorSpace::Linear);
    REQUIRE(d.useMipmaps);
}

TEST_CASE("TextureDesc: equality is field-by-field", "[texturedesc]") {
    TextureDesc a{};
    TextureDesc b{};
    REQUIRE(a == b);

    SECTION("size differs")       { b.size       = {2, 2};                 REQUIRE(a != b); }
    SECTION("format differs")     { b.format     = TextureFormat::RGB8;    REQUIRE(a != b); }
    SECTION("type differs")       { b.type       = TextureType::CubeMap;   REQUIRE(a != b); }
    SECTION("usage differs")      { b.usageFlags = TextureUsage::ColorAttachment; REQUIRE(a != b); }
    SECTION("colorSpace differs") { b.colorSpace = ColorSpace::sRGB;       REQUIRE(a != b); }
    SECTION("useMipmaps differs") { b.useMipmaps = false;                  REQUIRE(a != b); }
}

TEST_CASE("SamplerDesc: defaults match documented values", "[samplerdesc]") {
    SamplerDesc d{};
    REQUIRE(d.minFilter == MinFilter::LinearMipmapLinear);
    REQUIRE(d.magFilter == MagFilter::Linear);
    REQUIRE(d.wrapS     == TextureWrap::Repeat);
    REQUIRE(d.wrapT     == TextureWrap::Repeat);
    REQUIRE(d.wrapR     == TextureWrap::Repeat);
    REQUIRE_FALSE(d.depthCompare);
}

TEST_CASE("SamplerDesc: equality is field-by-field", "[samplerdesc]") {
    SamplerDesc a{};
    SamplerDesc b{};
    REQUIRE(a == b);

    SECTION("minFilter")    { b.minFilter    = MinFilter::Nearest;     REQUIRE(a != b); }
    SECTION("magFilter")    { b.magFilter    = MagFilter::Nearest;     REQUIRE(a != b); }
    SECTION("wrapS")        { b.wrapS        = TextureWrap::ClampToEdge; REQUIRE(a != b); }
    SECTION("wrapT")        { b.wrapT        = TextureWrap::ClampToEdge; REQUIRE(a != b); }
    SECTION("wrapR")        { b.wrapR        = TextureWrap::ClampToEdge; REQUIRE(a != b); }
    SECTION("depthCompare") { b.depthCompare = true;                   REQUIRE(a != b); }
}

TEST_CASE("SamplerDesc::requiresMipmaps: true for mipmap variants only", "[samplerdesc]") {
    SamplerDesc s{};

    SECTION("Nearest → false") {
        s.minFilter = MinFilter::Nearest;
        REQUIRE_FALSE(s.requiresMipmaps());
    }
    SECTION("Linear → false") {
        s.minFilter = MinFilter::Linear;
        REQUIRE_FALSE(s.requiresMipmaps());
    }
    SECTION("NearestMipmapNearest → true") {
        s.minFilter = MinFilter::NearestMipmapNearest;
        REQUIRE(s.requiresMipmaps());
    }
    SECTION("LinearMipmapNearest → true") {
        s.minFilter = MinFilter::LinearMipmapNearest;
        REQUIRE(s.requiresMipmaps());
    }
    SECTION("NearestMipmapLinear → true") {
        s.minFilter = MinFilter::NearestMipmapLinear;
        REQUIRE(s.requiresMipmaps());
    }
    SECTION("LinearMipmapLinear → true") {
        s.minFilter = MinFilter::LinearMipmapLinear;
        REQUIRE(s.requiresMipmaps());
    }
}

TEST_CASE("TextureUsage: bitwise OR yields a combined value", "[textureusage]") {
    // The enum is non-class so implicit int conversion makes bitwise ops work.
    const auto combined =
        static_cast<TextureUsage>(TextureUsage::Sampled | TextureUsage::ColorAttachment);

    REQUIRE((combined & TextureUsage::Sampled)         != 0);
    REQUIRE((combined & TextureUsage::ColorAttachment) != 0);
    REQUIRE((combined & TextureUsage::DepthAttachment) == 0);
}
