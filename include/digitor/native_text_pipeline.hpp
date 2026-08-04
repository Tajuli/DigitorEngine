#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace digitor {

enum class TextDirection : std::uint32_t { auto_detect, left_to_right, right_to_left };
enum class TextScript : std::uint32_t { auto_detect, latin, bengali, arabic, devanagari, common };

struct FontDescriptor {
  std::string family;
  std::string path;
  std::uint32_t face_index{};
  std::uint32_t weight{400};
  bool italic{};
};

struct ShapingRequest {
  std::string utf8;
  FontDescriptor primary_font;
  std::vector<FontDescriptor> fallback_fonts;
  TextDirection direction{TextDirection::auto_detect};
  TextScript script{TextScript::auto_detect};
  std::string language;
  double font_size{48.0};
  double letter_spacing{};
};

struct ShapedGlyph {
  std::uint32_t glyph_id{};
  std::uint32_t cluster{};
  std::uint32_t font_slot{};
  double x_advance{};
  double y_advance{};
  double x_offset{};
  double y_offset{};
};

struct ShapedRun {
  std::vector<ShapedGlyph> glyphs;
  TextDirection direction{TextDirection::left_to_right};
  TextScript script{TextScript::common};
  double advance_x{};
  double advance_y{};
  bool used_complex_shaping{};
  bool used_fallback{};
};

struct GlyphBitmap {
  std::uint32_t width{};
  std::uint32_t height{};
  std::int32_t bearing_x{};
  std::int32_t bearing_y{};
  std::int32_t advance_x{};
  std::vector<std::uint8_t> coverage;
};

struct AtlasEntry {
  std::uint32_t page{};
  std::uint32_t x{};
  std::uint32_t y{};
  std::uint32_t width{};
  std::uint32_t height{};
  float u0{};
  float v0{};
  float u1{};
  float v1{};
};

struct TextVertex {
  float x{};
  float y{};
  float u{};
  float v{};
  std::uint32_t rgba{0xffffffffu};
  std::uint32_t page{};
};

struct TextDrawPacket {
  std::vector<TextVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::uint32_t atlas_generation{};
  bool gpu_ready{};
};

using GlyphRasterizer = std::function<std::optional<GlyphBitmap>(const FontDescriptor&, std::uint32_t, double)>;

class NativeTextPipeline {
 public:
  explicit NativeTextPipeline(std::uint32_t atlas_width = 1024, std::uint32_t atlas_height = 1024);
  ShapedRun shape(const ShapingRequest&) const;
  TextDrawPacket prepare(const ShapingRequest&, const GlyphRasterizer&, std::uint32_t rgba = 0xffffffffu);
  std::uint32_t atlas_generation() const noexcept;
  std::size_t cached_glyphs() const noexcept;
  void clear();

 private:
  struct Impl;
  Impl* impl_;
};

bool native_text_dependencies_available() noexcept;
std::string native_text_dependency_report();

}  // namespace digitor
