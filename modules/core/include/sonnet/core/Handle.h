#pragma once

#include <cstddef>
#include <functional>
#include <limits>

namespace sonnet::core {

using OpaqueHandle = std::size_t;
static constexpr OpaqueHandle NULL_HANDLE = std::numeric_limits<OpaqueHandle>::max();

template <typename Tag>
struct Handle {
    OpaqueHandle value{NULL_HANDLE};

    [[nodiscard]] bool isValid() const noexcept { return value != NULL_HANDLE; }
    explicit operator bool() const noexcept { return isValid(); }
    auto operator<=>(const Handle &) const = default;
};

} // namespace sonnet::core

template <typename Tag>
struct std::hash<sonnet::core::Handle<Tag>> {
    std::size_t operator()(const sonnet::core::Handle<Tag> &h) const noexcept {
        return std::hash<sonnet::core::OpaqueHandle>{}(h.value);
    }
};
