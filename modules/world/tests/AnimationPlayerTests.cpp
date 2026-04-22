#include <sonnet/world/AnimationPlayer.h>
#include <sonnet/world/Transform.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <glm/gtc/quaternion.hpp>

using namespace sonnet::world;
using namespace sonnet::loaders;

// Build a minimal AnimationPlayer with one clip targeting a single Transform.
struct AnimFixture {
    Transform     transform;
    AnimationPlayer player;

    AnimFixture() {
        AnimationClip clip;
        clip.name     = "Test";
        clip.duration = 2.0f;
        player.clips.push_back(std::move(clip));
        player.addTarget("Node", &transform);
        player.loop    = true;
        player.playing = true;
    }

    AnimationChannel &channel() { return player.clips[0].channels[0]; }

    void addChannel() {
        player.clips[0].channels.push_back(AnimationChannel{.nodeName = "Node"});
    }
};

TEST_CASE("AnimationPlayer: update advances playback time", "[animation]") {
    AnimFixture f;
    f.addChannel();
    // Two position keyframes so there is something to interpolate.
    f.channel().positions = {{0.0f, {0.0f, 0.0f, 0.0f}}, {2.0f, {4.0f, 0.0f, 0.0f}}};

    REQUIRE_THAT(f.player.time, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
    f.player.update(0.5f);
    REQUIRE_THAT(f.player.time, Catch::Matchers::WithinAbs(0.5f, 1e-5f));
}

TEST_CASE("AnimationPlayer: linear position interpolated at midpoint", "[animation]") {
    AnimFixture f;
    f.addChannel();
    // 0s → (0,0,0), 2s → (4,0,0). At t=1s → (2,0,0).
    f.channel().positions = {{0.0f, {0.0f, 0.0f, 0.0f}}, {2.0f, {4.0f, 0.0f, 0.0f}}};

    f.player.update(1.0f);

    const glm::vec3 pos = f.transform.getLocalPosition();
    REQUIRE_THAT(pos.x, Catch::Matchers::WithinAbs(2.0f, 1e-4f));
    REQUIRE_THAT(pos.y, Catch::Matchers::WithinAbs(0.0f, 1e-4f));
}

TEST_CASE("AnimationPlayer: slerp rotation interpolated at midpoint", "[animation]") {
    AnimFixture f;
    f.addChannel();
    // identity → 90° around Y. At t=1s (midpoint) → ~45° around Y.
    const glm::quat q0 = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    const glm::quat q1 = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    f.channel().rotations = {{0.0f, q0}, {2.0f, q1}};

    f.player.update(1.0f);

    const glm::quat rot  = f.transform.getLocalRotation();
    const glm::quat mid  = glm::angleAxis(glm::radians(45.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    // Angle difference should be small.
    const float dot = std::abs(glm::dot(glm::normalize(rot), glm::normalize(mid)));
    REQUIRE(dot > 0.999f);
}

TEST_CASE("AnimationPlayer: playback loops when time exceeds clip duration", "[animation]") {
    AnimFixture f;
    f.addChannel();
    f.channel().positions = {{0.0f, {0.0f, 0.0f, 0.0f}}, {2.0f, {2.0f, 0.0f, 0.0f}}};

    // Advance past the end (duration = 2.0). Loop → time wraps to 0.5.
    f.player.update(2.5f);

    REQUIRE_THAT(f.player.time, Catch::Matchers::WithinAbs(0.5f, 1e-4f));
    REQUIRE(f.player.playing);
}

TEST_CASE("AnimationPlayer: non-loop stops at clip end", "[animation]") {
    AnimFixture f;
    f.addChannel();
    f.channel().positions = {{0.0f, {0.0f, 0.0f, 0.0f}}, {2.0f, {2.0f, 0.0f, 0.0f}}};
    f.player.loop = false;

    f.player.update(10.0f);

    REQUIRE_THAT(f.player.time, Catch::Matchers::WithinAbs(2.0f, 1e-4f));
    REQUIRE(!f.player.playing);
}

TEST_CASE("AnimationPlayer: no-op when clips empty", "[animation]") {
    Transform t;
    AnimationPlayer p;
    p.addTarget("Node", &t);
    REQUIRE_NOTHROW(p.update(1.0f));
    REQUIRE_THAT(t.getLocalPosition().x, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("AnimationPlayer: unknown target node is silently ignored", "[animation]") {
    AnimFixture f;
    f.addChannel();
    f.channel().nodeName  = "NoSuchNode";
    f.channel().positions = {{0.0f, {5.0f, 0.0f, 0.0f}}, {1.0f, {5.0f, 0.0f, 0.0f}}};

    REQUIRE_NOTHROW(f.player.update(0.5f));
    // Transform unchanged — channel targeted an unknown node.
    REQUIRE_THAT(f.transform.getLocalPosition().x, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}
