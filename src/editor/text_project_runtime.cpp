#include "digitor/text_project_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace digitor {
namespace {
float clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }
void blend(FrameF& frame, int x, int y, PixelF color, float coverage) {
  if (x < 0 || y < 0 || x >= static_cast<int>(frame.width) ||
      y >= static_cast<int>(frame.height)) return;
  PixelF& destination = frame.pixels[static_cast<std::size_t>(y) * frame.width + x];
  const float source_alpha = clamp01(color.a * coverage);
  const float output_alpha = source_alpha + destination.a * (1.0f - source_alpha);
  if (output_alpha <= 0.0f) {
    destination = {};
    return;
  }
  destination.r = clamp01((color.r * source_alpha + destination.r * destination.a *
                           (1.0f - source_alpha)) / output_alpha);
  destination.g = clamp01((color.g * source_alpha + destination.g * destination.a *
                           (1.0f - source_alpha)) / output_alpha);
  destination.b = clamp01((color.b * source_alpha + destination.b * destination.a *
                           (1.0f - source_alpha)) / output_alpha);
  destination.a = clamp01(output_alpha);
}
std::string escape(const std::string& value) {
  std::ostringstream out;
  for (const char c : value) {
    if (c == '\\' || c == '|' || c == '\n') out << '\\';
    out << (c == '\n' ? 'n' : c);
  }
  return out.str();
}
std::string unescape(const std::string& value) {
  std::string out;
  bool escaped = false;
  for (const char c : value) {
    if (escaped) {
      out.push_back(c == 'n' ? '\n' : c);
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else {
      out.push_back(c);
    }
  }
  if (escaped) throw std::invalid_argument("trailing project escape");
  return out;
}
std::vector<std::string> split(const std::string& line) {
  std::vector<std::string> fields;
  std::string field;
  bool escaped = false;
  for (const char c : line) {
    if (escaped) {
      field.push_back('\\');
      field.push_back(c);
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '|') {
      fields.push_back(unescape(field));
      field.clear();
    } else {
      field.push_back(c);
    }
  }
  if (escaped) field.push_back('\\');
  fields.push_back(unescape(field));
  return fields;
}
}  // namespace

bool GlyphBitmap::valid() const noexcept {
  return width > 0 && height > 0 && coverage.size() ==
      static_cast<std::size_t>(width) * height;
}

std::vector<std::uint32_t> decode_utf8(const std::string& text) {
  std::vector<std::uint32_t> codepoints;
  for (std::size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i]);
    std::uint32_t codepoint{};
    std::size_t count{};
    if (first < 0x80) { codepoint = first; count = 1; }
    else if ((first & 0xe0) == 0xc0) { codepoint = first & 0x1f; count = 2; }
    else if ((first & 0xf0) == 0xe0) { codepoint = first & 0x0f; count = 3; }
    else if ((first & 0xf8) == 0xf0) { codepoint = first & 0x07; count = 4; }
    else throw std::invalid_argument("invalid UTF-8 leading byte");
    if (i + count > text.size()) throw std::invalid_argument("truncated UTF-8");
    for (std::size_t j = 1; j < count; ++j) {
      const auto continuation = static_cast<unsigned char>(text[i + j]);
      if ((continuation & 0xc0) != 0x80) throw std::invalid_argument("invalid UTF-8 continuation");
      codepoint = (codepoint << 6) | (continuation & 0x3f);
    }
    if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
      throw std::invalid_argument("invalid UTF-8 codepoint");
    codepoints.push_back(codepoint);
    i += count;
  }
  return codepoints;
}

TextLayout layout_text(const std::string& text, const FontFace& font,
                       const TextRenderStyle& style) {
  if (font.units_per_em <= 0.0 || style.font_size <= 0.0)
    throw std::invalid_argument("invalid text metrics");
  TextLayout layout;
  const double scale = style.font_size / font.units_per_em;
  double pen_x = style.x;
  double pen_y = style.y + font.ascent * scale;
  double max_x = pen_x;
  for (const auto codepoint : decode_utf8(text)) {
    if (codepoint == '\n') {
      pen_x = style.x;
      pen_y += (font.ascent - font.descent) * scale * style.line_spacing;
      continue;
    }
    const auto it = font.glyphs.find(codepoint);
    if (it == font.glyphs.end() || !it->second.valid()) continue;
    const GlyphBitmap& glyph = it->second;
    layout.glyphs.push_back({&glyph, pen_x + glyph.bearing_x * scale,
                            pen_y - glyph.bearing_y * scale});
    pen_x += glyph.advance * scale + style.letter_spacing;
    max_x = std::max(max_x, pen_x);
  }
  layout.width = std::max(0.0, max_x - style.x);
  layout.height = std::max(style.font_size,
      pen_y - style.y - font.descent * scale);
  return layout;
}

void render_text(FrameF& destination, const TextLayout& layout,
                 const TextRenderStyle& style) {
  if (!destination.valid()) throw std::invalid_argument("invalid text destination");
  for (const auto& placed : layout.glyphs) {
    const GlyphBitmap& glyph = *placed.glyph;
    const double scale = style.font_size / std::max(1.0, glyph.height == 0 ? 1.0 :
        static_cast<double>(glyph.height));
    const int output_width = std::max(1, static_cast<int>(std::lround(glyph.width * scale)));
    const int output_height = std::max(1, static_cast<int>(std::lround(glyph.height * scale)));
    for (int y = 0; y < output_height; ++y) {
      for (int x = 0; x < output_width; ++x) {
        const auto source_x = std::min(glyph.width - 1,
            static_cast<std::uint32_t>(x / scale));
        const auto source_y = std::min(glyph.height - 1,
            static_cast<std::uint32_t>(y / scale));
        const float coverage = glyph.coverage[
            static_cast<std::size_t>(source_y) * glyph.width + source_x];
        const int target_x = static_cast<int>(std::floor(placed.x)) + x;
        const int target_y = static_cast<int>(std::floor(placed.y)) + y;
        if (style.shadow.a > 0.0f)
          blend(destination, target_x + static_cast<int>(std::lround(style.shadow_x)),
                target_y + static_cast<int>(std::lround(style.shadow_y)),
                style.shadow, coverage);
        if (style.stroke_width > 0.0) {
          const int radius = static_cast<int>(std::ceil(style.stroke_width));
          for (int offset_y = -radius; offset_y <= radius; ++offset_y)
            for (int offset_x = -radius; offset_x <= radius; ++offset_x)
              if (offset_x * offset_x + offset_y * offset_y <= radius * radius)
                blend(destination, target_x + offset_x, target_y + offset_y,
                      style.stroke, coverage);
        }
        blend(destination, target_x, target_y, style.fill, coverage);
      }
    }
  }
}

std::string serialize_project(const EditorProjectState& project) {
  if (project.schema_version != 1) throw std::invalid_argument("unsupported project schema");
  std::ostringstream out;
  out << "DIGITOR_PROJECT|1|" << escape(project.project_id) << '\n';
  out << std::setprecision(17);
  for (const auto& clip : project.clips) {
    const auto& v = clip.visual;
    out << "CLIP|" << escape(clip.clip_id) << '|' << v.transform.position.x << '|'
        << v.transform.position.y << '|' << v.transform.scale.x << '|'
        << v.transform.scale.y << '|' << v.transform.rotation_degrees << '|'
        << v.transform.opacity << '|' << v.crop.normalized.left << '|'
        << v.crop.normalized.top << '|' << v.crop.normalized.right << '|'
        << v.crop.normalized.bottom << '|' << escape(v.text.utf8_text) << '|'
        << escape(v.text.font_family) << '|' << v.text.font_size << '|'
        << static_cast<int>(v.chroma_key.enabled) << '|'
        << static_cast<int>(v.stabilization.enabled) << '\n';
  }
  return out.str();
}

EditorProjectState deserialize_project(const std::string& serialized) {
  std::istringstream input(serialized);
  std::string line;
  if (!std::getline(input, line)) throw std::invalid_argument("empty project");
  const auto header = split(line);
  if (header.size() != 3 || header[0] != "DIGITOR_PROJECT" || header[1] != "1")
    throw std::invalid_argument("invalid project header");
  EditorProjectState project;
  project.project_id = header[2];
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    const auto fields = split(line);
    if (fields.size() != 17 || fields[0] != "CLIP")
      throw std::invalid_argument("invalid project clip record");
    ProjectClipVisual clip;
    clip.clip_id = fields[1];
    auto& v = clip.visual;
    v.transform.position = {std::stod(fields[2]), std::stod(fields[3])};
    v.transform.scale = {std::stod(fields[4]), std::stod(fields[5])};
    v.transform.rotation_degrees = std::stod(fields[6]);
    v.transform.opacity = std::stod(fields[7]);
    v.crop.normalized = {std::stod(fields[8]), std::stod(fields[9]),
                         std::stod(fields[10]), std::stod(fields[11])};
    v.text.utf8_text = fields[12];
    v.text.font_family = fields[13];
    v.text.font_size = std::stod(fields[14]);
    v.chroma_key.enabled = std::stoi(fields[15]) != 0;
    v.stabilization.enabled = std::stoi(fields[16]) != 0;
    project.clips.push_back(std::move(clip));
  }
  return project;
}

ProgressTaskResult run_progress_task(std::uint32_t steps,
                                     const ProgressCallback& callback) {
  if (steps == 0) return {false, false, "progress task requires at least one step"};
  for (std::uint32_t step = 0; step <= steps; ++step) {
    const double progress = static_cast<double>(step) / steps;
    if (callback && !callback(progress, step == steps ? "complete" : "processing"))
      return {false, true, "cancelled by callback"};
  }
  return {true, false, {}};
}

}  // namespace digitor
