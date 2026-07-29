#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace digitor {

template <typename Integer>
inline bool checked_size_cast(std::size_t value, Integer& result) noexcept {
    static_assert(std::is_integral_v<Integer>);
    static_assert(!std::is_same_v<Integer, bool>);
    if (static_cast<std::uintmax_t>(value) >
        static_cast<std::uintmax_t>(std::numeric_limits<Integer>::max())) {
        return false;
    }
    result = static_cast<Integer>(value);
    return true;
}

inline bool checked_size_to_uint32(std::size_t value,
                                   std::uint32_t& result) noexcept {
    return checked_size_cast(value, result);
}

inline bool checked_size_to_int(std::size_t value, int& result) noexcept {
    return checked_size_cast(value, result);
}

inline double checked_size_to_double(std::size_t value) {
    constexpr std::uintmax_t maximum_exact = std::uintmax_t{1}
        << std::numeric_limits<double>::digits;
    if (static_cast<std::uintmax_t>(value) > maximum_exact) {
        throw std::overflow_error("size is not exactly representable as double");
    }
    return static_cast<double>(value);
}

inline float checked_size_to_float(std::size_t value) {
    constexpr std::uintmax_t maximum_exact = std::uintmax_t{1}
        << std::numeric_limits<float>::digits;
    if (static_cast<std::uintmax_t>(value) > maximum_exact) {
        throw std::overflow_error("size is not exactly representable as float");
    }
    return static_cast<float>(value);
}

}  // namespace digitor
