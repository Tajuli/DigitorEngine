#include "digitor/qualifier.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

using digitor::Color;
using digitor::HslQualifierParameters;
using digitor::QualifierSettings;

void test_hsl_qualifier_parameters_are_deterministic() {
  QualifierSettings settings;
  settings.hue = {0.92f, 0.08f, 0.04f};
  settings.saturation = {0.20f, 1.0f, 0.10f};
  settings.luminance = {0.05f, 0.95f, 0.08f};
  settings.invert = true;

  const auto first = HslQualifierParameters::create(settings);
  const auto second = HslQualifierParameters::create(settings);
  assert(first);
  assert(second);
  assert(first->serialize() == second->serialize());
  assert(first->identity() == second->identity());
}

void test_hsl_qualifier_rejects_invalid_parameters() {
  QualifierSettings settings;
  settings.hue.low = -0.01f;
  bool rejected = false;
  try {
    (void)HslQualifierParameters::create(settings);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  assert(rejected);

  settings = {};
  settings.saturation.softness = std::numeric_limits<float>::infinity();
  rejected = false;
  try {
    (void)HslQualifierParameters::create(settings);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  assert(rejected);
}

void test_hue_wrap_and_invert() {
  QualifierSettings settings;
  settings.hue = {0.90f, 0.10f, 0.0f};
  settings.saturation = {0.20f, 1.0f, 0.0f};
  settings.luminance = {0.0f, 1.0f, 0.0f};
  const auto normal = HslQualifierParameters::create(settings);

  const Color red{1.0f, 0.0f, 0.0f, 0.37f};
  const Color cyan{0.0f, 1.0f, 1.0f, 0.42f};
  assert(digitor::apply_hsl_qualifier_reference(red, *normal) == 1.0f);
  assert(digitor::apply_hsl_qualifier_reference(cyan, *normal) == 0.0f);

  settings.invert = true;
  const auto inverted = HslQualifierParameters::create(settings);
  assert(digitor::apply_hsl_qualifier_reference(red, *inverted) == 0.0f);
  assert(digitor::apply_hsl_qualifier_reference(cyan, *inverted) == 1.0f);
}

void test_reference_batch_and_counter() {
  const auto parameters = HslQualifierParameters::create();
  const std::array<Color, 3> input{{
      {0.0f, 0.0f, 0.0f, 1.0f},
      {0.5f, 0.25f, 0.1f, 1.0f},
      {1.0f, 1.0f, 1.0f, 1.0f},
  }};
  std::array<float, 3> output{};

  digitor::reset_hsl_qualifier_reference_count();
  digitor::apply_hsl_qualifier_reference(input, output, *parameters);
  assert(digitor::hsl_qualifier_reference_count() == input.size());
  for (const float value : output) {
    assert(std::isfinite(value));
    assert(value >= 0.0f && value <= 1.0f);
  }
}

struct QualifierTestRegistration {
  QualifierTestRegistration() {
    test_hsl_qualifier_parameters_are_deterministic();
    test_hsl_qualifier_rejects_invalid_parameters();
    test_hue_wrap_and_invert();
    test_reference_batch_and_counter();
  }
};

QualifierTestRegistration run_qualifier_tests;

} // namespace
