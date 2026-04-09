#include <catch2/catch_test_macros.hpp>

#include <sonnet/core/Macros.h>
#include <sonnet/core/Store.h>

using namespace sonnet::core;

// ── Fixtures ──────────────────────────────────────────────────────────────────

struct SimpleType {
    int x;
    float y;

    SimpleType() : SimpleType(0, 0.0f) {}
    SimpleType(int x, float y) : x(x), y(y) { ++constructorCalls; }

    SN_NON_COPYABLE(SimpleType);
    SN_NON_MOVABLE(SimpleType);

    static int constructorCalls;
};
int SimpleType::constructorCalls = 0;

struct ResetCounter {
    ResetCounter()  { SimpleType::constructorCalls = 0; }
    ~ResetCounter() { REQUIRE(SimpleType::constructorCalls <= 1); }
};

// ── Handle tests ──────────────────────────────────────────────────────────────

TEST_CASE("Default Handle is invalid", "[Handle]") {
    Handle<SimpleType> h;
    REQUIRE_FALSE(h.isValid());
    REQUIRE_FALSE(static_cast<bool>(h));
}

TEST_CASE("Handle with non-null value is valid", "[Handle]") {
    Handle<SimpleType> h{42};
    REQUIRE(h.isValid());
    REQUIRE(static_cast<bool>(h));
}

TEST_CASE("Handles of different tag types are distinct types", "[Handle]") {
    struct TagA {};
    struct TagB {};
    REQUIRE_FALSE((std::same_as<Handle<TagA>, Handle<TagB>>));
}

TEST_CASE("Handle equality and ordering", "[Handle]") {
    Handle<SimpleType> a{1};
    Handle<SimpleType> b{1};
    Handle<SimpleType> c{2};
    REQUIRE(a == b);
    REQUIRE(a != c);
    REQUIRE(a < c);
}

// ── Store basic tests ─────────────────────────────────────────────────────────

TEST_CASE("Store: tryGet on empty store returns nullopt", "[Store]") {
    ResetCounter _;
    Store<SimpleType> store;
    REQUIRE(store.tryGet({}) == std::nullopt);
    REQUIRE(store.tryGet({1}) == std::nullopt);
    REQUIRE(SimpleType::constructorCalls == 0);
}

TEST_CASE("Store: same args produce the same handle and object", "[Store]") {
    ResetCounter _;
    Store<SimpleType> store;

    const auto h1 = store.store(1, 2.0f);
    const auto h2 = store.store(1, 2.0f);

    REQUIRE(h1.isValid());
    REQUIRE(h1 == h2);
    REQUIRE(SimpleType::constructorCalls == 1);

    SimpleType &v1 = *store.tryGet(h1);
    SimpleType &v2 = *store.tryGet(h2);
    REQUIRE(&v1 == &v2);
    REQUIRE(v1.x == 1);
    REQUIRE(v1.y == 2.0f);
}

TEST_CASE("Store: different args produce different handles", "[Store]") {
    Store<SimpleType> store;
    const auto h1 = store.store(1, 2.0f);
    const auto h2 = store.store(3, 4.0f);
    REQUIRE(h1 != h2);
}

TEST_CASE("Store: default constructor via store()", "[Store]") {
    Store<SimpleType> store;
    const auto h1 = store.store();
    const auto h2 = store.store();
    REQUIRE(h1 == h2);
    REQUIRE(&store.get(h1).get() == &store.get(h2).get());
}

TEST_CASE("Store: get() throws when handle not found", "[Store]") {
    Store<SimpleType> store;
    REQUIRE_THROWS(store.get(Handle<SimpleType>{}));

    const auto h = store.store(1, 2.0f);
    REQUIRE_NOTHROW(store.get(h));
    REQUIRE(store.get(h).get().x == 1);
}

// ── Store with factory ────────────────────────────────────────────────────────

struct MovableType {
    std::string value;
    explicit MovableType(std::string v) : value(std::move(v)) { ++constructorCalls; }
    static int constructorCalls;
};
int MovableType::constructorCalls = 0;

TEST_CASE("Store: factory function is called at most once for same args", "[Store]") {
    MovableType::constructorCalls = 0;
    Store<MovableType> store;

    auto factory = [](std::string v) { return MovableType{std::move(v)}; };

    const auto h1 = store.store(factory, std::string{"hello"});
    const auto h2 = store.store(factory, std::string{"hello"});

    REQUIRE(h1 == h2);
    REQUIRE(MovableType::constructorCalls == 1);
    REQUIRE(store.tryGet(h1)->get().value == "hello");
}

// ── Store with abstract base ──────────────────────────────────────────────────

struct IShape {
    virtual ~IShape() = default;
    virtual std::string name() const = 0;
};

struct Circle final : IShape {
    Circle() = default;
    std::string name() const override { return "Circle"; }

    SN_NON_COPYABLE(Circle);
    SN_NON_MOVABLE(Circle);
};

TEST_CASE("Store: factory returning unique_ptr to base type", "[Store]") {
    Store<IShape> store;

    auto factory = [](int) -> std::unique_ptr<IShape> { return std::make_unique<Circle>(); };

    const auto h1 = store.store(factory, 1);
    const auto h2 = store.store(factory, 1);

    REQUIRE(h1 == h2);
    REQUIRE(&store.get(h1).get() == &store.get(h2).get());
    REQUIRE(store.get(h1).get().name() == "Circle");
}

TEST_CASE("Store: factory returning unique_ptr to derived type", "[Store]") {
    Store<IShape> store;

    auto factory = [](int) -> std::unique_ptr<Circle> { return std::make_unique<Circle>(); };

    const auto h1 = store.store(factory, 2);
    const auto h2 = store.store(factory, 2);

    REQUIRE(h1 == h2);
    REQUIRE(store.get(h1).get().name() == "Circle");
}

// ── tryStore ──────────────────────────────────────────────────────────────────

TEST_CASE("Store: tryStore returns nullopt when factory throws", "[Store]") {
    Store<MovableType> store;
    auto throws = [](std::string) -> MovableType { throw std::runtime_error("oops"); };
    REQUIRE_NOTHROW(store.tryStore(throws, std::string{"x"}));
    REQUIRE(store.tryStore(throws, std::string{"x"}) == std::nullopt);
}

// ── Constness ─────────────────────────────────────────────────────────────────

TEST_CASE("Store: tryGet propagates constness", "[Store]") {
    Store<SimpleType> store;
    const auto h = store.store(5, 6.0f);

    {
        const auto &cstore = store;
        auto opt = cstore.tryGet(h);
        using T = decltype(opt)::value_type::type;
        REQUIRE(std::is_const_v<T>);
    }
    {
        auto opt = store.tryGet(h);
        using T = decltype(opt)::value_type::type;
        REQUIRE_FALSE(std::is_const_v<T>);
    }
}

// ── drop ──────────────────────────────────────────────────────────────────────

TEST_CASE("Store: drop removes an entry", "[Store]") {
    Store<SimpleType> store;
    auto h = store.store(7, 8.0f);
    REQUIRE(store.tryGet(h).has_value());
    store.drop(h);
    REQUIRE_FALSE(h.isValid());
    REQUIRE(store.tryGet(h) == std::nullopt);
}
