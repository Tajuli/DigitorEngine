#include "digitor/native_text_pipeline.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

using namespace digitor;

namespace {
std::string bytes(std::initializer_list<std::uint8_t> values) {
  std::string result;
  result.reserve(values.size());
  for (const auto value : values) {
    result.push_back(static_cast<char>(value));
  }
  return result;
}
}  // namespace

int main() {
  NativeTextPipeline pipeline(128, 128);

  ShapingRequest bengali;
  // U+09AC BENGALI LETTER BA and U+09BE BENGALI VOWEL SIGN AA.
  bengali.utf8 = bytes({0xE0, 0xA6, 0xAC, 0xE0, 0xA6, 0xBE});
  bengali.primary_font = {"Noto Sans Bengali", "fixture.ttf", 0, 400, false};
  bengali.font_size = 32;

  const auto shaped = pipeline.shape(bengali);
  if (shaped.glyphs.empty() || shaped.script != TextScript::bengali ||
      !shaped.used_complex_shaping) {
    return 1;
  }

  auto raster = [](const FontDescriptor&, std::uint32_t glyph,
                   double) -> std::optional<GlyphBitmap> {
    GlyphBitmap bitmap;
    bitmap.width = 8;
    bitmap.height = 12;
    bitmap.advance_x = 9;
    bitmap.coverage.assign(
        96, static_cast<std::uint8_t>((glyph % 200u) + 55u));
    return bitmap;
  };

  const auto packet = pipeline.prepare(bengali, raster, 0xffffffffu);
  if (!packet.gpu_ready || packet.vertices.empty() ||
      packet.indices.size() % 6u != 0u) {
    return 2;
  }

  const auto cached = pipeline.cached_glyphs();
  if (cached == 0u) {
    return 3;
  }

  const auto packet2 = pipeline.prepare(bengali, raster);
  if (packet2.atlas_generation != packet.atlas_generation ||
      pipeline.cached_glyphs() != cached) {
    return 4;
  }

  ShapingRequest arabic;
  // U+0627 ARABIC LETTER ALEF and U+0644 ARABIC LETTER LAM.
  arabic.utf8 = bytes({0xD8, 0xA7, 0xD9, 0x84});
  arabic.primary_font = bengali.primary_font;
  const auto rtl = pipeline.shape(arabic);
  if (rtl.direction != TextDirection::right_to_left) {
    return 5;
  }

  bool rejected = false;
  try {
    ShapingRequest bad;
    bad.utf8 = bytes({0xC0, 0xAF});
    pipeline.shape(bad);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  if (!rejected) {
    return 6;
  }

  const auto before = pipeline.atlas_generation();
  pipeline.clear();
  if (pipeline.atlas_generation() <= before || pipeline.cached_glyphs() != 0u) {
    return 7;
  }

  std::cout << "BENGALI_SCRIPT_DETECTION=1\n"
               "RTL_DIRECTION=1\n"
               "GLYPH_ATLAS_CACHE=1\n"
               "GPU_DRAW_PACKET=1\n"
               "UTF8_REJECTION=1\n"
               "DEPENDENCIES="
            << native_text_dependency_report() << '\n';
  return 0;
}
