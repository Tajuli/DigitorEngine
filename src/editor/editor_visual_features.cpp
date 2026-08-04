#include "digitor/editor_visual_features.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace digitor {
namespace {

bool finite_non_negative(double value) {
  return std::isfinite(value) && value >= 0.0;
}

bool is_gpu_backend(VisualBackend backend) {
  return backend != VisualBackend::cpu;
}

void clamp_state(EditorVisualState& state) {
  state.transform.scale.x = std::max(0.0001, state.transform.scale.x);
  state.transform.scale.y = std::max(0.0001, state.transform.scale.y);
  state.transform.opacity = std::clamp(state.transform.opacity, 0.0, 1.0);
  state.crop.normalized.left = std::clamp(state.crop.normalized.left, 0.0, 1.0);
  state.crop.normalized.top = std::clamp(state.crop.normalized.top, 0.0, 1.0);
  state.crop.normalized.right = std::clamp(state.crop.normalized.right, 0.0, 1.0);
  state.crop.normalized.bottom = std::clamp(state.crop.normalized.bottom, 0.0, 1.0);
  if (state.crop.normalized.right < state.crop.normalized.left) {
    std::swap(state.crop.normalized.right, state.crop.normalized.left);
  }
  if (state.crop.normalized.bottom < state.crop.normalized.top) {
    std::swap(state.crop.normalized.bottom, state.crop.normalized.top);
  }
  state.crop.feather = std::clamp(state.crop.feather, 0.0, 1.0);
  state.chroma_key.similarity = std::clamp(state.chroma_key.similarity, 0.0, 1.0);
  state.chroma_key.softness = std::clamp(state.chroma_key.softness, 0.0, 1.0);
  state.chroma_key.spill = std::clamp(state.chroma_key.spill, 0.0, 1.0);
  state.stabilization.strength = std::clamp(state.stabilization.strength, 0.0, 1.0);
  state.stabilization.smoothing = std::clamp(state.stabilization.smoothing, 0.0, 1.0);
  state.stabilization.crop_ratio = std::clamp(state.stabilization.crop_ratio, 0.0, 0.5);
  state.text.font_size = std::max(1.0, state.text.font_size);
  state.text.line_spacing = std::max(0.1, state.text.line_spacing);
}

}  // namespace

void EditorVisualSession::apply(const EditorVisualCommand& command) {
  if (command.type == VisualCommandType::undo) {
    undo();
    return;
  }
  if (command.type == VisualCommandType::redo) {
    redo();
    return;
  }

  undo_.push_back(state_);
  redo_.clear();

  switch (command.type) {
    case VisualCommandType::set_transform:
      if (command.target_id == "position") state_.transform.position = {command.x, command.y};
      else if (command.target_id == "scale") state_.transform.scale = {command.x, command.y};
      else if (command.target_id == "anchor") state_.transform.anchor = {command.x, command.y};
      else if (command.target_id == "rotation") state_.transform.rotation_degrees = command.value;
      else if (command.target_id == "opacity") state_.transform.opacity = command.value;
      else if (command.target_id == "flip_x") state_.transform.flip_x = command.value != 0.0;
      else if (command.target_id == "flip_y") state_.transform.flip_y = command.value != 0.0;
      break;
    case VisualCommandType::set_crop:
      if (command.target_id == "left") state_.crop.normalized.left = command.value;
      else if (command.target_id == "top") state_.crop.normalized.top = command.value;
      else if (command.target_id == "right") state_.crop.normalized.right = command.value;
      else if (command.target_id == "bottom") state_.crop.normalized.bottom = command.value;
      else if (command.target_id == "feather") state_.crop.feather = command.value;
      break;
    case VisualCommandType::set_text:
      state_.text.utf8_text = command.text;
      break;
    case VisualCommandType::set_font:
      state_.text.font_family = command.text;
      if (command.value > 0.0) state_.text.font_size = command.value;
      break;
    case VisualCommandType::set_chroma_key:
      state_.chroma_key.enabled = true;
      if (command.target_id == "color") {
        state_.chroma_key.key_r = command.x;
        state_.chroma_key.key_g = command.y;
        state_.chroma_key.key_b = command.value;
      } else if (command.target_id == "similarity") state_.chroma_key.similarity = command.value;
      else if (command.target_id == "softness") state_.chroma_key.softness = command.value;
      else if (command.target_id == "spill") state_.chroma_key.spill = command.value;
      break;
    case VisualCommandType::set_stabilization:
      state_.stabilization.enabled = true;
      if (command.target_id == "strength") state_.stabilization.strength = command.value;
      else if (command.target_id == "smoothing") state_.stabilization.smoothing = command.value;
      else if (command.target_id == "crop") state_.stabilization.crop_ratio = command.value;
      break;
    case VisualCommandType::set_numeric_parameter:
      state_.numeric_fields.push_back(command.value);
      break;
    default:
      break;
  }
  clamp_state(state_);
}

bool EditorVisualSession::undo() {
  if (undo_.empty()) return false;
  redo_.push_back(state_);
  state_ = undo_.back();
  undo_.pop_back();
  return true;
}

bool EditorVisualSession::redo() {
  if (redo_.empty()) return false;
  undo_.push_back(state_);
  state_ = redo_.back();
  redo_.pop_back();
  return true;
}

VisualQualificationReport qualify_editor_visual_features(
    const std::vector<VisualFrameSample>& samples,
    const VisualQualificationLimits& limits) {
  VisualQualificationReport report;
  if (samples.empty()) {
    report.diagnostics.emplace_back("no editor visual feature samples supplied");
    return report;
  }
  if (!finite_non_negative(limits.max_channel_error) ||
      !finite_non_negative(limits.mean_channel_error) ||
      !finite_non_negative(limits.max_preview_frame_time_ms)) {
    report.diagnostics.emplace_back("invalid visual qualification limits");
    return report;
  }

  std::set<std::string> fixture_backend;
  for (const auto& sample : samples) {
    if (sample.fixture_id.empty() || sample.preview_digest.empty() || sample.export_digest.empty()) {
      report.diagnostics.emplace_back("invalid visual sample identity");
      continue;
    }
    const std::string key = sample.fixture_id + "#" + std::to_string(static_cast<std::uint32_t>(sample.backend));
    if (!fixture_backend.insert(key).second) {
      report.diagnostics.emplace_back("duplicate fixture/backend sample: " + sample.fixture_id);
    }
    if (!finite_non_negative(sample.max_channel_error) ||
        !finite_non_negative(sample.mean_channel_error) ||
        !finite_non_negative(sample.frame_time_ms)) {
      report.diagnostics.emplace_back("non-finite visual metric: " + sample.fixture_id);
      continue;
    }
    if (sample.max_channel_error > limits.max_channel_error) {
      report.diagnostics.emplace_back("maximum channel error exceeded: " + sample.fixture_id);
    }
    if (sample.mean_channel_error > limits.mean_channel_error) {
      report.diagnostics.emplace_back("mean channel error exceeded: " + sample.fixture_id);
    }
    if (sample.frame_time_ms > limits.max_preview_frame_time_ms) {
      report.diagnostics.emplace_back("smooth-preview frame budget exceeded: " + sample.fixture_id);
    }
    if (limits.require_gpu_for_gpu_backend && is_gpu_backend(sample.backend) && !sample.gpu_path_used) {
      report.diagnostics.emplace_back("GPU backend silently used non-GPU path: " + sample.fixture_id);
    }
    if (limits.require_preview_export_match && sample.preview_digest != sample.export_digest) {
      report.diagnostics.emplace_back("preview/export digest mismatch: " + sample.fixture_id);
    }
    if (limits.require_deterministic_output && !sample.deterministic) {
      report.diagnostics.emplace_back("non-deterministic output: " + sample.fixture_id);
    }
    if (limits.require_color_metadata && !sample.color_metadata_preserved) {
      report.diagnostics.emplace_back("color metadata not preserved: " + sample.fixture_id);
    }
  }
  report.qualified = report.diagnostics.empty();
  return report;
}

}  // namespace digitor
