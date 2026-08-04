#include "digitor/native_text_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace digitor {
namespace {

std::vector<std::uint32_t> decode_utf8(const std::string& value) {
  std::vector<std::uint32_t> out;
  for (std::size_t i = 0; i < value.size();) {
    const auto c = static_cast<unsigned char>(value[i]);
    std::uint32_t cp{};
    std::size_t n{};
    if (c < 0x80u) { cp = c; n = 1; }
    else if ((c & 0xe0u) == 0xc0u) { cp = c & 0x1fu; n = 2; }
    else if ((c & 0xf0u) == 0xe0u) { cp = c & 0x0fu; n = 3; }
    else if ((c & 0xf8u) == 0xf0u) { cp = c & 0x07u; n = 4; }
    else { throw std::invalid_argument("invalid UTF-8 lead byte"); }
    if (i + n > value.size()) throw std::invalid_argument("truncated UTF-8");
    for (std::size_t j = 1; j < n; ++j) {
      const auto cc = static_cast<unsigned char>(value[i + j]);
      if ((cc & 0xc0u) != 0x80u) throw std::invalid_argument("invalid UTF-8 continuation");
      cp = (cp << 6u) | (cc & 0x3fu);
    }
    out.push_back(cp);
    i += n;
  }
  return out;
}

TextScript detect_script(const std::vector<std::uint32_t>& cps) {
  for (const auto cp : cps) {
    if (cp >= 0x0980u && cp <= 0x09ffu) return TextScript::bengali;
    if (cp >= 0x0600u && cp <= 0x06ffu) return TextScript::arabic;
    if (cp >= 0x0900u && cp <= 0x097fu) return TextScript::devanagari;
    if ((cp >= 0x0041u && cp <= 0x024fu)) return TextScript::latin;
  }
  return TextScript::common;
}

TextDirection detect_direction(TextScript script) {
  return script == TextScript::arabic ? TextDirection::right_to_left : TextDirection::left_to_right;
}

bool is_combining(std::uint32_t cp) {
  return (cp >= 0x0300u && cp <= 0x036fu) || (cp >= 0x0981u && cp <= 0x0983u) ||
         (cp >= 0x09bcu && cp <= 0x09cdu) || (cp >= 0x064bu && cp <= 0x065fu);
}

struct GlyphKey {
  std::string path;
  std::uint32_t face{};
  std::uint32_t glyph{};
  std::uint32_t size_q{};
  bool operator==(const GlyphKey&) const = default;
};

struct GlyphKeyHash {
  std::size_t operator()(const GlyphKey& k) const noexcept {
    std::size_t h = std::hash<std::string>{}(k.path);
    h ^= std::size_t(k.face) * 0x9e3779b1u;
    h ^= std::size_t(k.glyph) * 0x85ebca6bu;
    h ^= std::size_t(k.size_q) * 0xc2b2ae35u;
    return h;
  }
};

}  // namespace

struct NativeTextPipeline::Impl {
  explicit Impl(std::uint32_t atlas_width, std::uint32_t atlas_height)
      : width(atlas_width), height(atlas_height) {}

  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t cursor_x{1};
  std::uint32_t cursor_y{1};
  std::uint32_t row_height{};
  std::uint32_t generation{1};
  std::unordered_map<GlyphKey, AtlasEntry, GlyphKeyHash> cache;

  AtlasEntry insert(const GlyphKey& key, const GlyphBitmap& bitmap) {
    if (bitmap.width + 2u > width || bitmap.height + 2u > height) {
      throw std::runtime_error("glyph exceeds atlas dimensions");
    }
    if (cursor_x + bitmap.width + 1u > width) {
      cursor_x = 1;
      cursor_y += row_height + 1u;
      row_height = 0;
    }
    if (cursor_y + bitmap.height + 1u > height) {
      cache.clear();
      cursor_x = 1;
      cursor_y = 1;
      row_height = 0;
      ++generation;
    }
    AtlasEntry entry;
    entry.x = cursor_x; entry.y = cursor_y;
    entry.width = bitmap.width; entry.height = bitmap.height;
    entry.u0 = float(entry.x) / float(width); entry.v0 = float(entry.y) / float(height);
    entry.u1 = float(entry.x + entry.width) / float(width);
    entry.v1 = float(entry.y + entry.height) / float(height);
    cursor_x += bitmap.width + 1u;
    row_height = std::max(row_height, bitmap.height);
    cache.emplace(key, entry);
    return entry;
  }
};

NativeTextPipeline::NativeTextPipeline(std::uint32_t atlas_width, std::uint32_t atlas_height)
    : impl_(new Impl(atlas_width, atlas_height)) {
  if (atlas_width < 32u || atlas_height < 32u) throw std::invalid_argument("atlas too small");
}

NativeTextPipeline::~NativeTextPipeline() { delete impl_; }

ShapedRun NativeTextPipeline::shape(const ShapingRequest& request) const {
  const auto cps = decode_utf8(request.utf8);
  ShapedRun run;
  run.script = request.script == TextScript::auto_detect ? detect_script(cps) : request.script;
  run.direction = request.direction == TextDirection::auto_detect ? detect_direction(run.script) : request.direction;
  run.used_complex_shaping = run.script == TextScript::bengali || run.script == TextScript::arabic || run.script == TextScript::devanagari;
  const double base_advance = std::max(1.0, request.font_size * 0.6 + request.letter_spacing);
  run.glyphs.reserve(cps.size());
  for (std::size_t i = 0; i < cps.size(); ++i) {
    ShapedGlyph glyph;
    glyph.glyph_id = cps[i];
    glyph.cluster = static_cast<std::uint32_t>(i);
    glyph.x_advance = is_combining(cps[i]) ? 0.0 : base_advance;
    glyph.x_offset = is_combining(cps[i]) ? -base_advance * 0.45 : 0.0;
    run.advance_x += glyph.x_advance;
    run.glyphs.push_back(glyph);
  }
  if (run.direction == TextDirection::right_to_left) std::reverse(run.glyphs.begin(), run.glyphs.end());
  return run;
}

TextDrawPacket NativeTextPipeline::prepare(const ShapingRequest& request, const GlyphRasterizer& rasterize, std::uint32_t rgba) {
  if (!rasterize) throw std::invalid_argument("glyph rasterizer is required");
  const auto run = shape(request);
  TextDrawPacket packet;
  double pen_x{};
  for (const auto& glyph : run.glyphs) {
    const FontDescriptor* font = &request.primary_font;
    GlyphKey key{font->path, font->face_index, glyph.glyph_id, static_cast<std::uint32_t>(std::llround(request.font_size * 64.0))};
    auto found = impl_->cache.find(key);
    GlyphBitmap bitmap;
    AtlasEntry entry;
    if (found == impl_->cache.end()) {
      auto rendered = rasterize(*font, glyph.glyph_id, request.font_size);
      if (!rendered) continue;
      bitmap = std::move(*rendered);
      if (bitmap.coverage.size() != std::size_t(bitmap.width) * bitmap.height) throw std::runtime_error("invalid glyph bitmap");
      entry = impl_->insert(key, bitmap);
    } else {
      entry = found->second;
      bitmap.width = entry.width;
      bitmap.height = entry.height;
    }
    const float x0 = float(pen_x + glyph.x_offset);
    const float y0 = float(glyph.y_offset);
    const float x1 = x0 + float(bitmap.width);
    const float y1 = y0 + float(bitmap.height);
    const auto base = static_cast<std::uint32_t>(packet.vertices.size());
    packet.vertices.insert(packet.vertices.end(), {{x0,y0,entry.u0,entry.v0,rgba,entry.page},{x1,y0,entry.u1,entry.v0,rgba,entry.page},{x1,y1,entry.u1,entry.v1,rgba,entry.page},{x0,y1,entry.u0,entry.v1,rgba,entry.page}});
    packet.indices.insert(packet.indices.end(), {base,base+1u,base+2u,base,base+2u,base+3u});
    pen_x += glyph.x_advance;
  }
  packet.atlas_generation = impl_->generation;
  packet.gpu_ready = !packet.vertices.empty();
  return packet;
}

std::uint32_t NativeTextPipeline::atlas_generation() const noexcept { return impl_->generation; }
std::size_t NativeTextPipeline::cached_glyphs() const noexcept { return impl_->cache.size(); }
void NativeTextPipeline::clear() { impl_->cache.clear(); ++impl_->generation; impl_->cursor_x = impl_->cursor_y = 1; impl_->row_height = 0; }

bool native_text_dependencies_available() noexcept {
#if defined(DIGITOR_HAS_FREETYPE) && defined(DIGITOR_HAS_HARFBUZZ)
  return true;
#else
  return false;
#endif
}

std::string native_text_dependency_report() {
  std::ostringstream out;
  out << "freetype=";
#if defined(DIGITOR_HAS_FREETYPE)
  out << "available";
#else
  out << "unavailable";
#endif
  out << ",harfbuzz=";
#if defined(DIGITOR_HAS_HARFBUZZ)
  out << "available";
#else
  out << "unavailable";
#endif
  return out.str();
}

}  // namespace digitor
