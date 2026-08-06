#include "digitor/cpu_simd.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

void test_copy_and_unaligned_tails() {
  for (std::size_t count = 0; count < 513; ++count) {
    std::vector<std::uint8_t> source(count + 7), output(count + 9, 0xcd);
    for (std::size_t i = 0; i < source.size(); ++i)
      source[i] = static_cast<std::uint8_t>((i * 37U + 11U) & 0xffU);
    digitor::simd_copy_bytes(source.data() + 3, output.data() + 5, count);
    assert(std::memcmp(source.data() + 3, output.data() + 5, count) == 0);
    for (std::size_t i = 0; i < 5; ++i) assert(output[i] == 0xcd);
  }
}

void test_fill_rgba8() {
  for (std::size_t pixels = 0; pixels < 259; ++pixels) {
    std::vector<std::uint8_t> output(pixels * 4U + 8U, 0xcd);
    digitor::simd_fill_opaque_black_rgba8(output.data(), pixels);
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
      assert(output[pixel * 4U + 0U] == 0U);
      assert(output[pixel * 4U + 1U] == 0U);
      assert(output[pixel * 4U + 2U] == 0U);
      assert(output[pixel * 4U + 3U] == 255U);
    }
    for (std::size_t i = pixels * 4U; i < output.size(); ++i)
      assert(output[i] == 0xcd);
  }
}

void test_blend_scalar_parity() {
  constexpr std::size_t count = 4099;
  std::vector<float> foreground(count), background(count), output(count);
  for (std::size_t i = 0; i < count; ++i) {
    foreground[i] = std::sin(static_cast<float>(i) * 0.013f);
    background[i] = std::cos(static_cast<float>(i) * 0.007f);
  }
  for (const float opacity : {-1.0f, 0.0f, 0.125f, 0.5f, 1.0f, 2.0f}) {
    digitor::simd_blend_rgba32f(foreground.data(), background.data(),
                                output.data(), count, opacity);
    const float a = std::clamp(opacity, 0.0f, 1.0f);
    for (std::size_t i = 0; i < count; ++i) {
      const float expected = foreground[i] * a + background[i] * (1.0f - a);
      assert(std::fabs(output[i] - expected) <= 1.0e-6f);
    }
  }
}

void test_detection_is_safe() {
  const auto caps = digitor::detect_cpu_simd_capabilities();
  const auto selected = digitor::selected_cpu_simd_level();
  assert(digitor::cpu_simd_level_name(selected) != nullptr);
  if (selected == digitor::CpuSimdLevel::avx2) assert(caps.avx2);
  if (selected == digitor::CpuSimdLevel::sse41) assert(caps.sse41);
  if (selected == digitor::CpuSimdLevel::sse2) assert(caps.sse2);
  if (selected == digitor::CpuSimdLevel::neon) assert(caps.neon);
  std::cout << "selected_simd=" << digitor::cpu_simd_level_name(selected)
            << " sse2=" << caps.sse2 << " sse41=" << caps.sse41
            << " avx2=" << caps.avx2 << " neon=" << caps.neon << '\n';
}

}  // namespace

int main() {
  test_copy_and_unaligned_tails();
  test_fill_rgba8();
  test_blend_scalar_parity();
  test_detection_is_safe();
  std::cout << "CPU SIMD qualification passed\n";
  return 0;
}
