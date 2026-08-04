#pragma once

#include "digitor/editor_visual_features.hpp"
#include "digitor/spatial_compositor.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace digitor {

struct GlyphBitmap {
  std::uint32_t codepoint{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::int32_t bearing_x{};
  std::int32_t bearing_y{};
  double advance{};
  std::vector<float> coverage;
  [[nodiscard]] bool valid() const noexcept;
};

struct FontFace {
  std::string family;
  double units_per_em{1000.0};
  double ascent{800.0};
  double descent{-200.0};
  std::unordered_map<std::uint32_t, GlyphBitmap> glyphs;
};

struct TextRenderStyle {
  double x{};
  double y{};
  double font_size{48.0};
  double letter_spacing{};
  double line_spacing{1.0};
  PixelF fill{1, 1, 1, 1};
  PixelF stroke{0, 0, 0, 1};
  double stroke_width{};
  PixelF shadow{0, 0, 0, 0};
  double shadow_x{};
  double shadow_y{};
};

struct TextLayoutGlyph {
  const GlyphBitmap* glyph{};
  double x{};
  double y{};
};

struct TextLayout {
  std::vector<TextLayoutGlyph> glyphs;
  double width{};
  double height{};
};

std::vector<std::uint32_t> decode_utf8(const std::string& text);
TextLayout layout_text(const std::string& text, const FontFace& font,
                       const TextRenderStyle& style);
void render_text(FrameF& destination, const TextLayout& layout,
                 const TextRenderStyle& style);

struct ProjectClipVisual {
  std::string clip_id;
  EditorVisualState visual;
};

struct EditorProjectState {
  std::uint32_t schema_version{1};
  std::string project_id;
  std::vector<ProjectClipVisual> clips;
};

std::string serialize_project(const EditorProjectState& project);
EditorProjectState deserialize_project(const std::string& serialized);

using ProgressCallback = std::function<bool(double, const std::string&)>;
struct ProgressTaskResult {
  bool completed{};
  bool cancelled{};
  std::string diagnostic;
};
ProgressTaskResult run_progress_task(std::uint32_t steps,
                                     const ProgressCallback& callback);

}  // namespace digitor
