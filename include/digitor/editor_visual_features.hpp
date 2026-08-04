#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class VisualBackend : std::uint32_t { vulkan, d3d12, metal, gles, cpu };
enum class VisualCommandType : std::uint32_t {
  set_transform, set_crop, set_text, set_font, set_chroma_key,
  set_stabilization, set_numeric_parameter, preview_play, preview_pause,
  undo, redo, import_media, export_media, touch_begin, touch_update, touch_end
};

enum class PreviewQuality : std::uint32_t { full, half, quarter, eighth };

struct Float2 { double x = 0.0; double y = 0.0; };
struct RectD { double left = 0.0; double top = 0.0; double right = 1.0; double bottom = 1.0; };

struct TransformState {
  Float2 position{};
  Float2 scale{1.0, 1.0};
  Float2 anchor{0.5, 0.5};
  double rotation_degrees = 0.0;
  double opacity = 1.0;
  bool flip_x = false;
  bool flip_y = false;
};

struct CropState {
  RectD normalized{};
  double feather = 0.0;
  bool preserve_aspect = false;
};

struct TextState {
  std::string utf8_text;
  std::string font_family;
  double font_size = 48.0;
  double letter_spacing = 0.0;
  double line_spacing = 1.0;
  std::uint32_t rgba = 0xffffffffu;
};

struct ChromaKeyState {
  double key_r = 0.0;
  double key_g = 1.0;
  double key_b = 0.0;
  double similarity = 0.35;
  double softness = 0.1;
  double spill = 0.5;
  bool enabled = false;
};

struct StabilizationState {
  double strength = 0.5;
  double smoothing = 0.5;
  double crop_ratio = 0.05;
  bool enabled = false;
};

struct EditorVisualState {
  TransformState transform;
  CropState crop;
  TextState text;
  ChromaKeyState chroma_key;
  StabilizationState stabilization;
  PreviewQuality preview_quality = PreviewQuality::full;
  std::vector<double> numeric_fields;
};

struct EditorVisualCommand {
  VisualCommandType type = VisualCommandType::set_transform;
  std::string target_id;
  std::string text;
  double value = 0.0;
  double x = 0.0;
  double y = 0.0;
};

struct VisualFrameSample {
  std::string fixture_id;
  VisualBackend backend = VisualBackend::cpu;
  std::string preview_digest;
  std::string export_digest;
  double max_channel_error = 0.0;
  double mean_channel_error = 0.0;
  double frame_time_ms = 0.0;
  bool gpu_path_used = false;
  bool deterministic = false;
  bool color_metadata_preserved = false;
};

struct VisualQualificationLimits {
  double max_channel_error = 1.0 / 255.0;
  double mean_channel_error = 0.25 / 255.0;
  double max_preview_frame_time_ms = 16.667;
  bool require_gpu_for_gpu_backend = true;
  bool require_preview_export_match = true;
  bool require_deterministic_output = true;
  bool require_color_metadata = true;
};

struct VisualQualificationReport {
  bool qualified = false;
  std::vector<std::string> diagnostics;
};

class EditorVisualSession {
 public:
  void apply(const EditorVisualCommand& command);
  bool undo();
  bool redo();
  const EditorVisualState& state() const noexcept { return state_; }
  std::size_t undo_depth() const noexcept { return undo_.size(); }
  std::size_t redo_depth() const noexcept { return redo_.size(); }

 private:
  EditorVisualState state_;
  std::vector<EditorVisualState> undo_;
  std::vector<EditorVisualState> redo_;
};

VisualQualificationReport qualify_editor_visual_features(
    const std::vector<VisualFrameSample>& samples,
    const VisualQualificationLimits& limits = {});

}  // namespace digitor
