#include "digitor/color.hpp"
#include "digitor/log_wheels.hpp"
#include "digitor/primary_wheels.hpp"

#include <bit>
#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

namespace {
bool exact(float a, float b) {
  return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}
void exact_color(const digitor::Color& a, const digitor::Color& b) {
  assert(exact(a.r, b.r));
  assert(exact(a.g, b.g));
  assert(exact(a.b, b.b));
  assert(exact(a.a, b.a));
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
}

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
  for (std::size_t i = 0; i < count; ++i) exact_color(actual[i], expected[i]);

  PrimaryWheelsDescriptor primary_desc{};
  primary_desc.lift = {0.07f, -0.02f, 0.03f};
  primary_desc.gamma = {1.8f, 2.2f, 0.73f};
  primary_desc.gain = {1.1f, 0.94f, 1.27f};
  primary_desc.offset = {-0.01f, 0.03f, 0.02f};
  const auto primary = PrimaryWheelsParameters::create(primary_desc);
  for (std::size_t i = 0; i < count; ++i)
    expected[i] = apply_primary_wheels_reference(input[i], *primary);
  apply_primary_wheels_reference(input, actual, *primary);
  for (std::size_t i = 0; i < count; ++i) exact_color(actual[i], expected[i]);

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
  for (std::size_t i = 0; i < count; ++i) exact_color(actual[i], expected[i]);

  return 0;
}
