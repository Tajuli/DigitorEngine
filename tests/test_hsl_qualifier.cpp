#include "digitor/qualifier.hpp"
#include "gpu/native_hsl_qualifier.hpp"
#include "digitor/commands.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

void test_hsl_qualifier() {
  using namespace digitor;

  reset_hsl_qualifier_reference_count();
  auto identity = HslQualifierParameters::create();
  assert(identity->is_identity());
  assert(!identity->identity().empty());
  assert(identity->serialize() == identity->identity());

  const Color red{1.0f, 0.0f, 0.0f, 0.25f};
  const Color green{0.0f, 1.0f, 0.0f, 0.50f};
  assert(apply_hsl_qualifier_reference(red, *identity) == 1.0f);
  assert(apply_hsl_qualifier_reference(green, *identity) == 1.0f);
  assert(hsl_qualifier_reference_count() == 2);

  QualifierSettings red_settings;
  red_settings.hue = {0.95f, 0.05f, 0.05f};
  red_settings.saturation = {0.5f, 1.0f, 0.1f};
  red_settings.luminance = {0.0f, 1.0f, 0.0f};
  auto red_key = HslQualifierParameters::create(red_settings);
  assert(apply_hsl_qualifier_reference(red, *red_key) > 0.99f);
  assert(apply_hsl_qualifier_reference(green, *red_key) < 0.01f);

  red_settings.invert = true;
  auto inverted = HslQualifierParameters::create(red_settings);
  assert(apply_hsl_qualifier_reference(red, *inverted) < 0.01f);
  assert(apply_hsl_qualifier_reference(green, *inverted) > 0.99f);

  red_settings.matte_output = true;
  auto packed_parameters = HslQualifierParameters::create(red_settings);
  const auto packed = native_hsl_qualifier_parameters(*packed_parameters, 7, 5);
  assert(packed.width == 7 && packed.height == 5 && packed.pixel_count == 35);
  assert((packed.flags & hsl_qualifier_flag_invert) != 0);
  assert((packed.flags & hsl_qualifier_flag_matte_output) != 0);
  assert(packed.hue[0] == 0.95f && packed.hue[1] == 0.05f);
  assert(!hsl_qualifier_shader_source().empty());
  assert(hsl_qualifier_shader_identity() ==
         "digitor-hsl-qualifier-v5.0.0-schema1");

  std::vector<Color> input{red, green};
  std::vector<float> matte(2);
  apply_hsl_qualifier_reference(input, matte, *red_key);
  assert(matte[0] > 0.99f && matte[1] < 0.01f);

  bool bad = false;
  auto invalid = red_settings;
  invalid.schema_version = 99;
  try { (void)HslQualifierParameters::create(invalid); }
  catch (const std::invalid_argument&) { bad = true; }
  assert(bad);

  bad = false;
  invalid = red_settings;
  invalid.blur = std::numeric_limits<float>::quiet_NaN();
  try { (void)HslQualifierParameters::create(invalid); }
  catch (const std::invalid_argument&) { bad = true; }
  assert(bad);

  HslQualifier legacy;
  legacy.set_settings(red_settings);
  CommandBuffer buffer;
  CommandEncoder encoder(buffer);
  bad = false;
  try { legacy.matte_gpu(encoder, input, matte, 2, 1); }
  catch (const std::logic_error&) { bad = true; }
  assert(bad);
}
