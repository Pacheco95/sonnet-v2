#pragma once

#include "Handle.h"
#include "Hash.h"

#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace sonnet::core {

namespace detail {
template <typename T>
struct is_unique_ptr : std::false_type {};
template <typename T>
struct is_unique_ptr<std::unique_ptr<T>> : std::true_type {};
template <typename T>
inline constexpr bool is_unique_ptr_v = is_unique_ptr<T>::value;

// Preserves constness of From onto To.
template <typename From, typename To>
using matchesConstness = std::conditional_t<std::is_const_v<std::remove_reference_t<From>>, const To, To>;
} // namespace detail

template <typename T>
class Store {
public:
    using HandleType = Handle<T>;

    // Store by constructing T in-place from args.
    template <typename... Args>
    HandleType store(Args &&...args) {
        const std::size_t hash = hashMany(std::forward<Args>(args)...);
        const HandleType handle{hash};

        if (m_cache.contains(handle)) {
            return handle;
        }

        m_cache.emplace(handle, std::make_unique<T>(std::forward<Args>(args)...));
        return handle;
    }

    // Store using a factory callable; factory may return T, unique_ptr<T>, or unique_ptr<Derived>.
    template <typename F, typename First, typename... Rest>
        requires std::invocable<F, First, Rest...>
    HandleType store(F &&factory, First &&first, Rest &&...rest) {
        const std::size_t hash = hashMany(std::forward<First>(first), std::forward<Rest>(rest)...);
        const HandleType handle{hash};

        if (m_cache.contains(handle)) {
            return handle;
        }

        using Ret = std::invoke_result_t<F, First, Rest...>;
        if constexpr (detail::is_unique_ptr_v<Ret>) {
            m_cache.emplace(
                handle,
                std::invoke(std::forward<F>(factory), std::forward<First>(first), std::forward<Rest>(rest)...));
        } else {
            m_cache.emplace(
                handle,
                std::make_unique<T>(
                    std::invoke(std::forward<F>(factory), std::forward<First>(first), std::forward<Rest>(rest)...)));
        }

        return handle;
    }

    // Like store(factory, ...) but returns nullopt if the factory throws.
    template <typename F, typename First, typename... Rest>
        requires std::invocable<F, First, Rest...>
    std::optional<HandleType> tryStore(F &&factory, First &&first, Rest &&...rest) {
        try {
            return store(std::forward<F>(factory), std::forward<First>(first), std::forward<Rest>(rest)...);
        } catch (...) {
            return std::nullopt;
        }
    }

    // Returns a reference to the stored value; throws std::out_of_range if not found.
    template <typename Self>
    auto get(this Self &&self, const HandleType &handle)
        -> std::reference_wrapper<detail::matchesConstness<Self, T>> {
        auto &cache = std::forward<Self>(self).m_cache;
        auto it = cache.find(handle);
        if (it == cache.end()) {
            throw std::out_of_range("sonnet::core::Store::get — handle not found");
        }
        return std::ref(*it->second);
    }

    // Returns an optional reference; nullopt if handle is invalid or not found.
    template <typename Self>
    auto tryGet(this Self &&self, const HandleType &handle)
        -> std::optional<std::reference_wrapper<detail::matchesConstness<Self, T>>> {
        if (!handle.isValid()) {
            return std::nullopt;
        }
        auto &cache = std::forward<Self>(self).m_cache;
        auto it = cache.find(handle);
        if (it == cache.end()) {
            return std::nullopt;
        }
        return std::ref(*it->second);
    }

    // Removes the entry and invalidates the handle.
    void drop(HandleType &handle) {
        if (!handle.isValid()) {
            return;
        }
        m_cache.erase(handle);
        handle.value = NULL_HANDLE;
    }

private:
    std::unordered_map<HandleType, std::unique_ptr<T>> m_cache;
};

} // namespace sonnet::core
