#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

#include "gpu/gpu_backend.hpp"

namespace {
using Pixel = std::array<std::uint8_t, 4>;

std::vector<std::uint8_t> solid(std::uint32_t width, std::uint32_t height, Pixel color) {
    std::vector<std::uint8_t> pixels(std::size_t(width) * height * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4)
        std::copy(color.begin(), color.end(), pixels.begin() + static_cast<std::ptrdiff_t>(i));
    return pixels;
}

std::vector<std::uint8_t> pattern(std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint8_t> pixels(std::size_t(width) * height * 4);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto offset = (std::size_t(y) * width + x) * 4;
            pixels[offset] = static_cast<std::uint8_t>((x * 17 + y * 3) & 0xff);
            pixels[offset + 1] = static_cast<std::uint8_t>((x * 5 + y * 53) & 0xff);
            pixels[offset + 2] = static_cast<std::uint8_t>((x * 101 + y * 7) & 0xff);
            pixels[offset + 3] = static_cast<std::uint8_t>(128 + ((x + y) & 0x7f));
        }
    return pixels;
}

bool compare(std::string_view backend, std::string_view operation, std::uint32_t width,
             std::uint32_t height, const std::vector<std::uint8_t>& expected,
             const std::vector<std::uint8_t>& actual) {
    std::size_t mismatches = 0;
    std::size_t first = 0;
    const auto pixels = std::min(expected.size(), actual.size()) / 4;
    for (std::size_t i = 0; i < pixels; ++i) {
        if (!std::equal_n(expected.data() + i * 4, 4, actual.data() + i * 4)) {
            if (mismatches++ == 0) first = i;
        }
    }
    const auto expected_pixels = (expected.size() + 3) / 4;
    const auto actual_pixels = (actual.size() + 3) / 4;
    if (expected_pixels != actual_pixels) {
        if (mismatches == 0) first = pixels;
        mismatches += expected_pixels > actual_pixels ? expected_pixels - actual_pixels
                                                      : actual_pixels - expected_pixels;
    }
    if (mismatches == 0) return true;
    const auto x = width ? first % width : 0;
    const auto y = width ? first / width : 0;
    auto print_pixel = [](const std::vector<std::uint8_t>& bytes, std::size_t index) {
        if ((index + 1) * 4 > bytes.size()) { std::cerr << "<missing>"; return; }
        std::cerr << '(' << unsigned(bytes[index * 4]) << ',' << unsigned(bytes[index * 4 + 1])
                  << ',' << unsigned(bytes[index * 4 + 2]) << ','
                  << unsigned(bytes[index * 4 + 3]) << ')';
    };
    std::cerr << "PIXEL MISMATCH backend=" << backend << " operation=" << operation
              << " dimensions=" << width << 'x' << height
              << " expected_bytes=" << expected.size() << " actual_bytes=" << actual.size()
              << " first_coordinate=(" << x << ',' << y << ") expected=";
    print_pixel(expected, first);
    std::cerr << " actual=";
    print_pixel(actual, first);
    std::cerr << " mismatching_pixels=" << mismatches << '\n';
    return false;
}

bool exercise(digitor::IRenderBackend& backend, std::string_view name) {
    constexpr std::array dimensions{std::pair{1u,1u}, std::pair{2u,2u}, std::pair{3u,2u},
        std::pair{7u,5u}, std::pair{63u,17u}, std::pair{65u,3u}, std::pair{257u,2u}};
    constexpr std::array colors{Pixel{255,0,0,255}, Pixel{0,255,0,255}, Pixel{0,0,255,255},
        Pixel{0,0,0,255}, Pixel{17,53,101,211}};
    bool passed = true;
    for (auto [width, height] : dimensions) {
        for (const auto color : colors) {
            const auto expected = solid(width, height, color);
            std::vector<std::uint8_t> actual;
            const auto result = backend.render_rgba8(width, height, expected, actual);
            if (result != DIGITOR_RESULT_OK) {
                std::cerr << "RENDER FAILURE backend=" << name << " operation=upload/copy dimensions="
                          << width << 'x' << height << " result=" << result << '\n';
                passed = false;
            } else passed &= compare(name, "upload/copy", width, height, expected, actual);
        }
        const auto expected_pattern = pattern(width, height);
        std::vector<std::uint8_t> actual;
        const auto upload_result = backend.render_rgba8(width, height, expected_pattern, actual);
        if (upload_result != DIGITOR_RESULT_OK) passed = false;
        else passed &= compare(name, "gradient upload/copy", width, height, expected_pattern, actual);

        const auto expected_clear = solid(width, height, Pixel{0,0,0,255});
        actual.clear();
        const auto clear_result = backend.render_rgba8(width, height, {}, actual);
        if (clear_result != DIGITOR_RESULT_OK) {
            std::cerr << "RENDER FAILURE backend=" << name << " operation=clear dimensions="
                      << width << 'x' << height << " result=" << clear_result << '\n';
            passed = false;
        } else passed &= compare(name, "clear", width, height, expected_clear, actual);
    }
    std::cerr << "BACKEND RESULT backend=" << name << " status=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed;
}
} // namespace

int main() {
#if defined(_WIN32)
    bool all_passed = true;
    bool d3d12_available = false;
    for (const auto entry : {std::pair{DIGITOR_RENDERER_VULKAN, std::string_view{"Vulkan"}},
                             std::pair{DIGITOR_RENDERER_D3D12, std::string_view{"Direct3D12"}}}) {
        auto backend = digitor::create_native_backend(entry.first);
        if (!backend || !backend->initialize(true)) {
            std::cerr << "BACKEND UNAVAILABLE backend=" << entry.second << '\n';
            continue;
        }
        if (entry.first == DIGITOR_RENDERER_D3D12) d3d12_available = true;
        all_passed &= exercise(*backend, entry.second);
        backend->shutdown();
    }
    if (!d3d12_available) {
        std::cerr << "Direct3D12 is required for the Windows native GPU test.\n";
        return 1;
    }
    return all_passed ? 0 : 1;
#else
    std::cout << "Native Windows GPU integration test is Windows-only.\n";
    return 0;
#endif
}
