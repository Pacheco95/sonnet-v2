#include <catch2/catch_test_macros.hpp>

#include <sonnet/api/render/RenderState.h>

using namespace sonnet::api::render;

TEST_CASE("RenderState: defaults match documented values", "[renderstate]") {
    RenderState s{};
    REQUIRE(s.depthTest);
    REQUIRE(s.depthWrite);
    REQUIRE(s.depthFunc == DepthFunction::Less);
    REQUIRE(s.blend     == BlendMode::Opaque);
    REQUIRE(s.cull      == CullMode::Back);
    REQUIRE(s.fill      == FillMode::Solid);
}

TEST_CASE("RenderState: equality is field-by-field", "[renderstate]") {
    RenderState a{};
    RenderState b{};
    REQUIRE(a == b);

    b.depthTest = false;
    REQUIRE(a != b);
}

TEST_CASE("RenderState: ordering via <=> is total", "[renderstate]") {
    RenderState a{};
    RenderState b{};
    b.cull = CullMode::Front;
    REQUIRE((a <=> b) != 0);
    REQUIRE((a <=> a) == 0);
}

TEST_CASE("RenderState::Hasher: equal states hash equally", "[renderstate][hash]") {
    RenderState a{};
    RenderState b{};
    RenderState::Hasher h{};
    REQUIRE(h(a) == h(b));
}

TEST_CASE("RenderState::Hasher: each single-field flip changes the hash", "[renderstate][hash]") {
    RenderState::Hasher h{};
    const std::size_t base = h(RenderState{});

    auto flipped = [](auto mutate) {
        RenderState s{};
        mutate(s);
        return s;
    };

    REQUIRE(h(flipped([](auto &s){ s.depthTest  = false; }))                   != base);
    REQUIRE(h(flipped([](auto &s){ s.depthWrite = false; }))                   != base);
    REQUIRE(h(flipped([](auto &s){ s.depthFunc  = DepthFunction::LessEqual; })) != base);
    REQUIRE(h(flipped([](auto &s){ s.blend      = BlendMode::Alpha; }))         != base);
    REQUIRE(h(flipped([](auto &s){ s.cull       = CullMode::Front; }))          != base);
    REQUIRE(h(flipped([](auto &s){ s.fill       = FillMode::Wireframe; }))      != base);
}

TEST_CASE("RenderOverrides: defaults are all empty", "[renderstate][overrides]") {
    RenderOverrides o{};
    REQUIRE_FALSE(o.fill.has_value());
    REQUIRE_FALSE(o.depthTest.has_value());
    REQUIRE_FALSE(o.cull.has_value());
}

TEST_CASE("RenderOverrides: setting one optional leaves others empty", "[renderstate][overrides]") {
    RenderOverrides o{};
    o.cull = CullMode::None;

    REQUIRE(o.cull == CullMode::None);
    REQUIRE_FALSE(o.fill.has_value());
    REQUIRE_FALSE(o.depthTest.has_value());
}
