#include "digitor/text_project_runtime.hpp"
#include "digitor/text_project_runtime_c.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
int progress_callback(double progress, const char*, void* user_data) {
  auto* count = static_cast<int*>(user_data);
  ++(*count);
  return progress < 0.75 ? 1 : 0;
}
int fail(int code, const char* message) {
  std::cerr << message << '\n';
  return code;
}
}  // namespace

int main() {
  using namespace digitor;
  const std::string bangla = "বাংলা Text";
  const auto codepoints = decode_utf8(bangla);
  if (codepoints.size() < 6) return fail(1, "UTF-8 decode failed");
  if (digitor_validate_utf8(bangla.c_str()) != DIGITOR_TEXT_PROJECT_OK)
    return fail(2, "C ABI UTF-8 validation failed");
  const char invalid_utf8[] = {static_cast<char>(0xc3), static_cast<char>(0x28), 0};
  if (digitor_validate_utf8(invalid_utf8) == DIGITOR_TEXT_PROJECT_OK)
    return fail(3, "invalid UTF-8 accepted");

  FontFace font;
  font.family = "Qualification";
  GlyphBitmap glyph;
  glyph.codepoint = 'A';
  glyph.width = 2;
  glyph.height = 2;
  glyph.bearing_y = 2;
  glyph.advance = 2;
  glyph.coverage = {1, 1, 1, 1};
  font.glyphs.emplace('A', glyph);
  TextRenderStyle style;
  style.font_size = 2;
  style.x = 1;
  style.y = 1;
  style.fill = {1, 0, 0, 1};
  const auto layout = layout_text("A", font, style);
  FrameF frame{8, 8, std::vector<PixelF>(64, {0, 0, 0, 1})};
  render_text(frame, layout, style);
  bool rendered = false;
  for (const auto& pixel : frame.pixels) rendered = rendered || pixel.r > 0.5f;
  if (!rendered) return fail(4, "text render failed");

  EditorProjectState project;
  project.project_id = "project|বাংলা";
  ProjectClipVisual clip;
  clip.clip_id = "clip-1";
  clip.visual.transform.position = {0.25, 0.5};
  clip.visual.text.utf8_text = bangla;
  clip.visual.text.font_family = "Noto Sans Bengali";
  clip.visual.text.font_size = 64;
  clip.visual.chroma_key.enabled = true;
  project.clips.push_back(clip);
  const std::string serialized = serialize_project(project);
  const auto restored = deserialize_project(serialized);
  if (restored.project_id != project.project_id || restored.clips.size() != 1 ||
      restored.clips[0].visual.text.utf8_text != bangla)
    return fail(5, "project roundtrip failed");

  size_t required = 0;
  if (digitor_project_roundtrip(serialized.c_str(), nullptr, 0, &required) !=
          DIGITOR_TEXT_PROJECT_BUFFER_TOO_SMALL || required == 0)
    return fail(6, "C ABI size query failed");
  std::vector<char> output(required);
  if (digitor_project_roundtrip(serialized.c_str(), output.data(), output.size(),
                                &required) != DIGITOR_TEXT_PROJECT_OK)
    return fail(7, "C ABI project roundtrip failed");
  if (std::strcmp(output.data(), serialized.c_str()) != 0)
    return fail(8, "C ABI project output mismatch");

  const auto complete = run_progress_task(4, [](double, const std::string&) { return true; });
  if (!complete.completed || complete.cancelled)
    return fail(9, "progress completion failed");
  int callback_count = 0;
  if (digitor_run_progress_task(8, progress_callback, &callback_count) !=
          DIGITOR_TEXT_PROJECT_CANCELLED || callback_count < 2)
    return fail(10, "progress cancellation failed");

  std::cout << "UTF8_BANGLA=1\nTEXT_LAYOUT_RASTER=1\nPROJECT_ROUNDTRIP=1\n"
               "PROGRESS_CALLBACK=1\nC_ABI=1\n";
  return 0;
}
