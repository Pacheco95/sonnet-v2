#pragma once

#include <cstddef>
#include <functional>
#include <ranges>
#include <variant>

namespace sonnet::core {

template <typename T>
concept HasOwnHasher = requires { typename T::Hasher; };

template <HasOwnHasher T>
void hashCombine(std::size_t &seed, const T &v) {
    typename T::Hasher hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template <typename T>
void hashCombine(std::size_t &seed, const T &v) {
    seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template <typename... Types>
void hashCombine(std::size_t &seed, const std::variant<Types...> &v) {
    hashCombine(seed, v.index());
    std::visit([&seed](const auto &val) { hashCombine(seed, val); }, v);
}

template <std::ranges::forward_range R>
void hashCombine(std::size_t &seed, const R &range) {
    for (const auto &element : range) {
        hashCombine(seed, element);
    }
}

template <typename... Args>
std::size_t hashMany(Args &&...args) {
    std::size_t seed = 0;
    (hashCombine(seed, std::forward<Args>(args)), ...);
    return seed;
}

} // namespace sonnet::core
