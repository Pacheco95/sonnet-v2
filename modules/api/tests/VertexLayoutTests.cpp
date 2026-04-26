#include <catch2/catch_test_macros.hpp>

#include <sonnet/api/render/VertexLayout.h>

using namespace sonnet::api::render;

namespace {

VertexLayout makeLayout(KnownAttributeSet attrs) {
    return VertexLayout{std::move(attrs)};
}

} // namespace

TEST_CASE("VertexLayout: stride sums attribute sizes", "[vertex][layout]") {
    SECTION("Position only") {
        auto layout = makeLayout({PositionAttribute{}});
        REQUIRE(layout.getStride() == sizeof(glm::vec3));
    }
    SECTION("Position + Normal + TexCoord") {
        auto layout = makeLayout({PositionAttribute{}, NormalAttribute{}, TexCoordAttribute{}});
        REQUIRE(layout.getStride() ==
                sizeof(glm::vec3) + sizeof(glm::vec3) + sizeof(glm::vec2));
    }
    SECTION("Full PBR + skinning") {
        auto layout = makeLayout({
            PositionAttribute{}, ColorAttribute{}, TexCoordAttribute{},
            NormalAttribute{}, TangentAttribute{}, BiTangentAttribute{},
            BoneIndexAttribute{}, BoneWeightAttribute{},
        });
        const std::size_t expected =
            sizeof(glm::vec3) + sizeof(glm::vec4) + sizeof(glm::vec2) +
            sizeof(glm::vec3) + sizeof(glm::vec3) + sizeof(glm::vec3) +
            sizeof(glm::ivec4) + sizeof(glm::vec4);
        REQUIRE(layout.getStride() == expected);
    }
}

TEST_CASE("VertexLayout: empty attribute set yields zero stride", "[vertex][layout]") {
    auto layout = makeLayout({});
    REQUIRE(layout.getStride() == 0);
    REQUIRE(layout.getAttributes().empty());
}

TEST_CASE("VertexLayout: insertion order does not affect stride (set is location-ordered)",
         "[vertex][layout]") {
    auto a = makeLayout({PositionAttribute{}, NormalAttribute{}, TexCoordAttribute{}});
    auto b = makeLayout({TexCoordAttribute{}, NormalAttribute{}, PositionAttribute{}});
    REQUIRE(a.getStride() == b.getStride());
}

TEST_CASE("VertexLayout: KnownAttributeSet deduplicates by location", "[vertex][layout]") {
    KnownAttributeSet attrs{};
    attrs.insert(PositionAttribute{});
    attrs.insert(PositionAttribute{});
    REQUIRE(attrs.size() == 1);

    auto layout = makeLayout(std::move(attrs));
    REQUIRE(layout.getStride() == sizeof(glm::vec3));
}

TEST_CASE("VertexLayout: getAttributes iterates in location order", "[vertex][layout]") {
    auto layout = makeLayout({TexCoordAttribute{}, PositionAttribute{}, NormalAttribute{}});
    const auto &attrs = layout.getAttributes();
    REQUIRE(attrs.size() == 3);

    auto it = attrs.begin();
    REQUIRE(std::visit([](auto &&a) { return std::decay_t<decltype(a)>::location; }, *it++) == 0);
    REQUIRE(std::visit([](auto &&a) { return std::decay_t<decltype(a)>::location; }, *it++) == 2);
    REQUIRE(std::visit([](auto &&a) { return std::decay_t<decltype(a)>::location; }, *it++) == 3);
}

TEST_CASE("VertexLayout::Hasher: equal stride hashes equally", "[vertex][layout][hash]") {
    auto a = makeLayout({PositionAttribute{}, NormalAttribute{}});
    auto b = makeLayout({PositionAttribute{}, NormalAttribute{}});

    VertexLayout::Hasher h{};
    REQUIRE(h(a) == h(b));
}

TEST_CASE("VertexLayout::Hasher: different stride hashes differently", "[vertex][layout][hash]") {
    auto a = makeLayout({PositionAttribute{}});
    auto b = makeLayout({PositionAttribute{}, NormalAttribute{}});

    VertexLayout::Hasher h{};
    REQUIRE(h(a) != h(b));
}
