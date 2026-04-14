#pragma once

#include <sonnet/core/Hash.h>

#include <algorithm>
#include <set>
#include <string_view>
#include <variant>

#include <glm/glm.hpp>

namespace sonnet::api::render {

template <std::size_t N>
struct ConstexprStr {
    char data[N]{};
    constexpr ConstexprStr(const char (&s)[N]) { std::copy_n(s, N, data); } // NOLINT
    constexpr operator std::string_view() const { return {data, N - 1}; }   // NOLINT
};

template <ConstexprStr Tag, unsigned Location, typename T>
struct VertexAttribute {
    static_assert(std::is_trivially_copyable_v<T>);

    using ValueType     = T;
    using ComponentType = typename T::value_type;

    static constexpr unsigned       location       = Location;
    static constexpr auto           componentCount = T::length();
    static constexpr auto           sizeInBytes    = sizeof(T);
    static constexpr std::string_view name         = Tag;

    T    value{};
    bool normalize = false;

    template <ConstexprStr Tag2, unsigned Loc2, typename T2>
    constexpr auto operator<=>(const VertexAttribute<Tag2, Loc2, T2> &other) const {
        using U = VertexAttribute<Tag2, Loc2, T2>;
        static_assert(std::is_same_v<T, T2>);
        if (auto c = location <=> U::location; c != 0) return c;
        return name <=> U::name;
    }

    struct Hasher {
        std::size_t operator()(const VertexAttribute &v) const noexcept {
            std::size_t seed = 0;
            core::hashCombine(seed, v.value);
            core::hashCombine(seed, v.normalize);
            return seed;
        }
    };
};

// clang-format off
using PositionAttribute  = VertexAttribute<"Position",  0, glm::vec3>;
using ColorAttribute     = VertexAttribute<"Color",     1, glm::vec4>;
using TexCoordAttribute  = VertexAttribute<"TexCoord",  2, glm::vec2>;
using NormalAttribute    = VertexAttribute<"Normal",    3, glm::vec3>;
using TangentAttribute   = VertexAttribute<"Tangent",   4, glm::vec3>;
using BiTangentAttribute = VertexAttribute<"BiTangent", 5, glm::vec3>;
using BoneIndexAttribute = VertexAttribute<"BoneIndex", 6, glm::ivec4>;
using BoneWeightAttribute= VertexAttribute<"BoneWeight",7, glm::vec4>;

template <typename T>
concept Attribute =
    std::same_as<T, PositionAttribute>   ||
    std::same_as<T, ColorAttribute>      ||
    std::same_as<T, TexCoordAttribute>   ||
    std::same_as<T, NormalAttribute>     ||
    std::same_as<T, TangentAttribute>    ||
    std::same_as<T, BiTangentAttribute>  ||
    std::same_as<T, BoneIndexAttribute>  ||
    std::same_as<T, BoneWeightAttribute>;

inline constexpr auto Position  = PositionAttribute{};
inline constexpr auto Color     = ColorAttribute{};
inline constexpr auto TexCoord  = TexCoordAttribute{};
inline constexpr auto Normal    = NormalAttribute{};
inline constexpr auto Tangent   = TangentAttribute{};
inline constexpr auto BiTangent = BiTangentAttribute{};

using KnownAttribute = std::variant<
    PositionAttribute,
    ColorAttribute,
    TexCoordAttribute,
    NormalAttribute,
    TangentAttribute,
    BiTangentAttribute,
    BoneIndexAttribute,
    BoneWeightAttribute
>;
// clang-format on

struct AttributeLocationComparator {
    using is_transparent = void;
    bool operator()(const KnownAttribute &lhs, const KnownAttribute &rhs) const {
        auto loc = []([[maybe_unused]] auto &&a) { return std::decay_t<decltype(a)>::location; };
        return std::visit(loc, lhs) < std::visit(loc, rhs);
    }
};

using KnownAttributeSet = std::set<KnownAttribute, AttributeLocationComparator>;

} // namespace sonnet::api::render
