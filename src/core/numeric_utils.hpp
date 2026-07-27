#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace digitor {

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
