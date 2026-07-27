#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace digitor {

// Copies as much of source as fits and always terminates a non-empty buffer.
inline void copy_bounded(char* destination, std::size_t capacity,
                         std::string_view source) noexcept {
    if (destination == nullptr || capacity == 0) return;
    const std::size_t length = std::min(source.size(), capacity - 1);
    if (length != 0) std::memcpy(destination, source.data(), length);
    destination[length] = '\0';
}

template <std::size_t Capacity>
inline void copy_bounded(char (&destination)[Capacity], std::string_view source) noexcept {
    copy_bounded(destination, Capacity, source);
}

}  // namespace digitor
