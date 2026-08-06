#include "digitor/color.hpp"
#include "digitor/log_wheels.hpp"
#include "digitor/primary_wheels.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {
bool exact(float a, float b) {
  return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

bool exact_color(const digitor::Color& actual, const digitor::Color& expected,
                 const char* stage, std::size_t index) {
  if (exact(actual.r, expected.r) && exact(actual.g, expected.g) &&
      exact(actual.b, expected.b) && exact(actual.a, expected.a)) {
    return true;
  }
  std::cerr << stage << " bit-exact mismatch at pixel " << index << '\n';
  return false;
}

std::vector<digitor::Color> fixture(std::size_t count) {
  std::vector<digitor::Color> values(count);
  for (std::size_t i = 0; i < count; ++i) {
    const float x = static_cast<float>(i % 4093U) / 2046.0f - 1.0f;
    const float y = static_cast<float>((i * 17U) % 4091U) / 2045.0f - 1.0f;
    const float z = static_cast<float>((i * 31U) % 4087U) / 2043.0f - 1.0f;
    values[i] = {x, y, z, static_cast<float>(i % 251U) / 250.0f};
  }
  values[3].r = std::numeric_limits<float>::infinity();
  values[7].g = std::numeric_limits<float>::quiet_NaN();
  return values;
}

bool verify(std::span<const digitor::Color> actual,
            std::span<const digitor::Color> expected, const char* stage) {
  if (actual.size() != expected.size()) {
    std::cerr << stage << " size mismatch\n";
    return false;
  }
  for (std::size_t i = 0; i < actual.size(); ++i) {
    if (!exact_color(actual[i], expected[i], stage, i)) return false;
  }
  return true;
}
}  // namespace

int main() {
  using namespace digitor;
  constexpr std::size_t count = 131071U;
  const auto input = fixture(count);
  std::vector<Color> expected(count), actual(count);

  ColorGrade grade{};
  grade.exposure = 1.375f;
  grade.gamma = 2.2f;
  grade.hue = 137.25f;
  grade.contrast = 1.17f;
  grade.saturation = 1.23f;
  grade.temperature = -0.31f;
  grade.tint = 0.27f;
  grade.lift = -0.08f;
  grade.gain = 1.14f;
  grade.offset = 0.019f;
  for (std::size_t i = 0; i < count; ++i) expected[i] = grade_color(input[i], grade);
  grade_image_cpu(input.data(), actual.data(), count, grade);
  if (!verify(actual, expected, "grade")) return 1;

  PrimaryWheelsDescriptor primary_desc{};
  primary_desc.lift = {0.07f, -0.02f, 0.03f};
  primary_desc.gamma = {1.8f, 2.2f, 0.73f};
  primary_desc.gain = {1.1f, 0.94f, 1.27f};
  primary_desc.offset = {-0.01f, 0.03f, 0.02f};
  const auto primary = PrimaryWheelsParameters::create(primary_desc);
  for (std::size_t i = 0; i < count; ++i)
    expected[i] = apply_primary_wheels_reference(input[i], *primary);
  apply_primary_wheels_reference(input, actual, *primary);
  if (!verify(actual, expected, "primary-wheels")) return 1;

  LogWheelsDescriptor log_desc{};
  log_desc.shadows.master = -0.7f;
  log_desc.midtones.master = 0.43f;
  log_desc.highlights.master = 1.13f;
  log_desc.global.master = 0.21f;
  log_desc.shadows.rgb = {0.03f, -0.02f, 0.01f};
  log_desc.midtones.rgb = {-0.01f, 0.04f, 0.02f};
  log_desc.highlights.rgb = {0.02f, 0.01f, -0.03f};
  const auto log = LogWheelsParameters::create(log_desc);
  for (std::size_t i = 0; i < count; ++i)
    expected[i] = apply_log_wheels_reference(input[i], *log);
  apply_log_wheels_reference(input, actual, *log);
  if (!verify(actual, expected, "log-wheels")) return 1;

  std::cout << "Transcendental CPU parallel bit-exact qualification passed\n";
  return 0;
}
