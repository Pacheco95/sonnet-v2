#include <catch2/catch_test_macros.hpp>

#include <sonnet/api/input/Key.h>

using sonnet::api::input::Key;
using sonnet::api::input::KeyMap;

TEST_CASE("KeyMap: insert then get round-trips", "[keymap]") {
    KeyMap<int> map;
    REQUIRE(map.insert(32, Key::Space));
    REQUIRE(map.insert(257, Key::Enter));
    REQUIRE(map.insert(65, Key::A));

    REQUIRE(map.get(32)  == Key::Space);
    REQUIRE(map.get(257) == Key::Enter);
    REQUIRE(map.get(65)  == Key::A);
    REQUIRE(map.size() == 3);
}

TEST_CASE("KeyMap: get returns nullopt for absent key", "[keymap]") {
    KeyMap<int> map;
    map.insert(32, Key::Space);
    REQUIRE(map.get(999) == std::nullopt);
}

TEST_CASE("KeyMap: get on empty map returns nullopt", "[keymap]") {
    KeyMap<int> map;
    REQUIRE(map.get(0) == std::nullopt);
    REQUIRE(map.size() == 0);
}

TEST_CASE("KeyMap: re-inserting the same key updates value, size unchanged", "[keymap]") {
    KeyMap<int> map;
    REQUIRE(map.insert(42, Key::A));
    REQUIRE(map.size() == 1);
    REQUIRE(map.insert(42, Key::B));
    REQUIRE(map.size() == 1);
    REQUIRE(map.get(42) == Key::B);
}

TEST_CASE("KeyMap: handles linear-probe collisions", "[keymap]") {
    // Capacity == static_cast<size_t>(Key::Last). Two keys whose values are
    // congruent modulo Capacity will hash to the same slot and force a probe.
    KeyMap<int> map;
    constexpr int capacity = static_cast<int>(Key::Last);
    const int a = 7;
    const int b = 7 + capacity;

    REQUIRE(map.insert(a, Key::A));
    REQUIRE(map.insert(b, Key::B));
    REQUIRE(map.size() == 2);
    REQUIRE(map.get(a) == Key::A);
    REQUIRE(map.get(b) == Key::B);
}

TEST_CASE("KeyMap: full table — insert returns false, lookups still work", "[keymap]") {
    KeyMap<int> map;
    constexpr auto capacity = static_cast<int>(Key::Last);

    for (int i = 0; i < capacity; ++i) {
        REQUIRE(map.insert(i, Key::A));
    }
    REQUIRE(map.size() == static_cast<std::size_t>(capacity));

    // One more — table is full, must refuse.
    REQUIRE_FALSE(map.insert(capacity, Key::B));
    REQUIRE(map.size() == static_cast<std::size_t>(capacity));

    // Earlier entries still retrievable.
    REQUIRE(map.get(0) == Key::A);
    REQUIRE(map.get(capacity - 1) == Key::A);
}

TEST_CASE("KeyMap: get terminates when table is full and key is absent", "[keymap]") {
    // The probe loop has a `start == idx` guard. If the table is full and the
    // key isn't present, get() must return nullopt rather than infinite-loop.
    KeyMap<int> map;
    constexpr auto capacity = static_cast<int>(Key::Last);

    // Fill all capacity slots with keys 0..capacity-1.
    for (int i = 0; i < capacity; ++i) {
        REQUIRE(map.insert(i, Key::A));
    }

    REQUIRE(map.get(capacity) == std::nullopt);
    REQUIRE(map.get(capacity * 2 + 5) == std::nullopt);
}

TEST_CASE("KeyMap: works with unsigned integral key type", "[keymap]") {
    KeyMap<unsigned> map;
    REQUIRE(map.insert(0u, Key::Zero));
    REQUIRE(map.insert(123456u, Key::Nine));
    REQUIRE(map.get(0u)      == Key::Zero);
    REQUIRE(map.get(123456u) == Key::Nine);
}

TEST_CASE("KeyMap: size reflects only successful inserts of new keys", "[keymap]") {
    KeyMap<int> map;
    map.insert(1, Key::A);
    map.insert(2, Key::B);
    map.insert(1, Key::C); // overwrite
    map.insert(3, Key::D);
    REQUIRE(map.size() == 3);
    REQUIRE(map.get(1) == Key::C);
}
