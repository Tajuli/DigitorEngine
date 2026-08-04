#include "digitor/editor_visual_features.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

int main() {
  using namespace digitor;

  EditorVisualSession session;
  session.apply({VisualCommandType::set_transform, "scale", {}, 0.0, 1.25, 1.25});
  session.apply({VisualCommandType::set_crop, "left", {}, 0.1});
  session.apply({VisualCommandType::set_text, {}, "বাংলা Title"});
  session.apply({VisualCommandType::set_font, {}, "Noto Sans Bengali", 56.0});
  session.apply({VisualCommandType::set_chroma_key, "color", {}, 0.0, 0.0, 1.0});
  session.apply({VisualCommandType::set_chroma_key, "similarity", {}, 0.42});
  session.apply({VisualCommandType::set_stabilization, "strength", {}, 0.75});
  session.apply({VisualCommandType::set_numeric_parameter, "exposure", {}, 0.2});

  if (std::abs(session.state().transform.scale.x - 1.25) > 1e-12) return 1;
  if (std::abs(session.state().crop.normalized.left - 0.1) > 1e-12) return 2;
  if (session.state().text.utf8_text.empty() || session.state().text.font_family.empty()) return 3;
  if (!session.state().chroma_key.enabled || !session.state().stabilization.enabled) return 4;
  if (!session.undo()) return 5;
  if (!session.redo()) return 6;

  const std::string digest(64, 'a');
  std::vector<VisualFrameSample> samples = {
      {"transform-crop", VisualBackend::vulkan, digest, digest, 0.001, 0.0002, 8.0, true, true, true},
      {"text-overlay", VisualBackend::d3d12, digest, digest, 0.001, 0.0002, 9.0, true, true, true},
      {"chroma-key", VisualBackend::metal, digest, digest, 0.001, 0.0002, 7.0, true, true, true},
      {"stabilization", VisualBackend::gles, digest, digest, 0.001, 0.0002, 12.0, true, true, true},
      {"cpu-fallback", VisualBackend::cpu, digest, digest, 0.001, 0.0002, 15.0, false, true, true},
  };

  const auto ok = qualify_editor_visual_features(samples);
  if (!ok.qualified) return 7;

  auto bad = samples;
  bad[0].gpu_path_used = false;
  if (qualify_editor_visual_features(bad).qualified) return 8;
  bad = samples;
  bad[1].export_digest = std::string(64, 'b');
  if (qualify_editor_visual_features(bad).qualified) return 9;
  bad = samples;
  bad[2].frame_time_ms = 20.0;
  if (qualify_editor_visual_features(bad).qualified) return 10;
  bad = samples;
  bad[3].max_channel_error = 0.02;
  if (qualify_editor_visual_features(bad).qualified) return 11;

  std::cout << "EDITOR_VISUAL_FEATURE_CONTRACT=1\n"
            << "TRANSFORM_CROP_TEXT_CHROMA_STABILIZATION=1\n"
            << "UNDO_REDO_TOUCH_COMMANDS=1\n"
            << "GPU_FIRST_NO_SILENT_CPU_FALLBACK=1\n"
            << "PER_PIXEL_PREVIEW_EXPORT_PARITY=1\n"
            << "SMOOTH_PREVIEW_FRAME_BUDGET=1\n";
  return 0;
}
