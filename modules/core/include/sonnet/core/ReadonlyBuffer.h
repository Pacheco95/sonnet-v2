#pragma once

#include "Hash.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace sonnet::core {

template <typename T>
class ReadonlyBuffer {
public:
    explicit ReadonlyBuffer(std::vector<T> data) : m_data(std::move(data)) {
        hashCombine(m_hash, m_data);
    }

    explicit ReadonlyBuffer(const T *data, std::size_t size)
        : ReadonlyBuffer(std::vector<T>(data, data + size)) {}

    [[nodiscard]] const T &operator[](std::size_t i) const noexcept { return m_data[i]; }
    [[nodiscard]] const T &at(std::size_t i) const { return m_data.at(i); }
    [[nodiscard]] const T *data() const noexcept { return m_data.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_data.size(); }
    [[nodiscard]] std::size_t bytes() const noexcept { return m_data.size() * sizeof(T); }
    [[nodiscard]] bool empty() const noexcept { return m_data.empty(); }
    [[nodiscard]] std::size_t hash() const noexcept { return m_hash; }

    bool operator==(const ReadonlyBuffer &other) const noexcept {
        return m_hash == other.m_hash && m_data == other.m_data;
    }
    auto operator<=>(const ReadonlyBuffer &other) const noexcept { return m_hash <=> other.m_hash; }

private:
    std::vector<T> m_data{};
    std::size_t m_hash{0};
};

} // namespace sonnet::core

template <typename T>
struct std::hash<sonnet::core::ReadonlyBuffer<T>> {
    std::size_t operator()(const sonnet::core::ReadonlyBuffer<T> &b) const noexcept {
        return b.hash();
    }
};
