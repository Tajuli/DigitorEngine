#include "digitor/plugin_zero_copy_frame.hpp"

#include <utility>

namespace digitor {
namespace {

bool same_encoding(const PluginGpuFrame& a, const PluginGpuFrame& b) noexcept {
  return a.width == b.width && a.height == b.height &&
         a.format == b.format && a.primaries == b.primaries &&
         a.transfer == b.transfer && a.range == b.range &&
         a.alpha == b.alpha && a.timestamp_us == b.timestamp_us;
}

PluginZeroCopyFrameRuntime::ParityRecord make_record(
    const PluginZeroCopyRequest& request) {
  PluginZeroCopyFrameRuntime::ParityRecord out{};
  out.stack_digest = request.visual_stack_digest;
  out.plugin_id = request.instance.plugin_id;
  out.plugin_version = request.instance.plugin_version;
  out.format = request.output.format;
  out.primaries = request.output.primaries;
  out.transfer = request.output.transfer;
  out.range = request.output.range;
  out.alpha = request.output.alpha;
  return out;
}

bool same_record(const PluginZeroCopyFrameRuntime::ParityRecord& a,
                 const PluginZeroCopyFrameRuntime::ParityRecord& b) noexcept {
  return a.stack_digest == b.stack_digest && a.plugin_id == b.plugin_id &&
         a.plugin_version == b.plugin_version && a.format == b.format &&
         a.primaries == b.primaries && a.transfer == b.transfer &&
         a.range == b.range && a.alpha == b.alpha;
}

}  // namespace

PluginZeroCopyFrameRuntime::PluginZeroCopyFrameRuntime(
    PluginZeroCopyBindings bindings)
    : bindings_(std::move(bindings)) {}

bool PluginZeroCopyFrameRuntime::validate_request(
    const PluginZeroCopyRequest& request,
    std::string& diagnostic) const noexcept {
  if (request.instance.plugin_id.empty() ||
      request.instance.plugin_version.empty() ||
      request.project_or_clip_id.empty() || request.visual_stack_digest.empty()) {
    diagnostic = "plugin zero-copy request identity is incomplete";
    return false;
  }
  if (request.input.backend != bindings_.selected_backend ||
      request.output.backend != bindings_.selected_backend) {
    diagnostic = "plugin frame backend differs from selected GPU backend";
    return false;
  }
  if (request.input.native_texture_handle == 0 ||
      request.output.native_texture_handle == 0) {
    diagnostic = "plugin frame requires native GPU texture handles";
    return false;
  }
  if (request.input.width == 0 || request.input.height == 0 ||
      !same_encoding(request.input, request.output)) {
    diagnostic = "plugin output must preserve dimensions, timestamp and color encoding";
    return false;
  }
  diagnostic.clear();
  return true;
}

bool PluginZeroCopyFrameRuntime::validate_parity(
    const PluginZeroCopyRequest& request,
    std::string& diagnostic) noexcept {
  const auto record = make_record(request);
  auto& own = request.surface == ConsumerPluginSurface::preview
      ? preview_records_ : export_records_;
  auto& other = request.surface == ConsumerPluginSurface::preview
      ? export_records_ : preview_records_;
  own[request.output.timestamp_us] = record;
  const auto it = other.find(request.output.timestamp_us);
  if (it != other.end() && !same_record(record, it->second)) {
    ++telemetry_.parity_failures;
    diagnostic = "plugin preview/export stack or color encoding mismatch";
    return false;
  }
  diagnostic.clear();
  return true;
}

DigitorResult PluginZeroCopyFrameRuntime::process(
    const PluginZeroCopyRequest& request,
    std::string* diagnostic) noexcept {
  std::string local;
  if (!validate_request(request, local) || !validate_parity(request, local)) {
    telemetry_.diagnostic = local;
    if (diagnostic) *diagnostic = local;
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (!bindings_.dispatch) {
    local = "plugin GPU dispatch binding is unavailable";
    telemetry_.diagnostic = local;
    if (diagnostic) *diagnostic = local;
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  const DigitorResult result = bindings_.dispatch(request, local);
  if (result != DIGITOR_RESULT_OK) {
    telemetry_.diagnostic = local.empty()
        ? "plugin GPU dispatch failed without CPU fallback" : local;
    if (diagnostic) *diagnostic = telemetry_.diagnostic;
    return result;
  }
  ++telemetry_.gpu_dispatches;
  if (request.surface == ConsumerPluginSurface::preview)
    ++telemetry_.preview_frames;
  else
    ++telemetry_.export_frames;
  telemetry_.diagnostic.clear();
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

PluginZeroCopyTelemetry PluginZeroCopyFrameRuntime::telemetry() const {
  return telemetry_;
}

}  // namespace digitor
