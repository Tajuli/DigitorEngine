#include "digitor/plugin_zero_copy_stack.hpp"

#include <utility>

namespace digitor {
namespace {

bool same_encoding(const PluginGpuFrame& a, const PluginGpuFrame& b) noexcept {
  return a.backend == b.backend && a.width == b.width &&
         a.height == b.height && a.format == b.format &&
         a.primaries == b.primaries && a.transfer == b.transfer &&
         a.range == b.range && a.alpha == b.alpha &&
         a.timestamp_us == b.timestamp_us;
}

}  // namespace

PluginZeroCopyStackRuntime::PluginZeroCopyStackRuntime(
    PluginZeroCopyFrameRuntime& frame_runtime,
    PluginZeroCopyInstanceResolver resolver)
    : frame_runtime_(frame_runtime), resolver_(std::move(resolver)) {}

bool PluginZeroCopyStackRuntime::validate_request(
    const PluginZeroCopyStackRequest& request,
    std::string& diagnostic) const noexcept {
  if (request.project_or_clip_id.empty() ||
      request.visual_stack_digest.empty() || request.instances.empty()) {
    diagnostic = "plugin stack identity or ordered instances are missing";
    return false;
  }
  if (request.intermediates.size() + 1 != request.instances.size()) {
    diagnostic = "plugin stack requires exactly one GPU target per pass";
    return false;
  }
  if (!same_encoding(request.source, request.destination) ||
      request.source.native_texture_handle == 0 ||
      request.destination.native_texture_handle == 0) {
    diagnostic = "plugin stack source/destination encoding or handles are invalid";
    return false;
  }
  for (const auto& frame : request.intermediates) {
    if (!same_encoding(request.source, frame) ||
        frame.native_texture_handle == 0) {
      diagnostic = "plugin stack intermediate must preserve native GPU encoding";
      return false;
    }
  }
  for (const auto& instance : request.instances) {
    if (!instance.enabled || instance.instance_id.empty() ||
        instance.plugin_id.empty() || instance.plugin_version.empty()) {
      diagnostic = "plugin stack contains an invalid or disabled instance";
      return false;
    }
  }
  diagnostic.clear();
  return true;
}

DigitorResult PluginZeroCopyStackRuntime::process(
    const PluginZeroCopyStackRequest& request,
    std::string* diagnostic) noexcept {
  std::string local;
  if (!validate_request(request, local)) {
    ++telemetry_.failed_frames;
    telemetry_.diagnostic = local;
    if (diagnostic) *diagnostic = local;
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }

  PluginGpuFrame input = request.source;
  for (std::size_t index = 0; index < request.instances.size(); ++index) {
    const auto& instance = request.instances[index];
    if (resolver_ && !resolver_(instance, local)) {
      ++telemetry_.failed_frames;
      telemetry_.diagnostic = local.empty()
          ? "plugin stack instance resolution failed" : local;
      if (diagnostic) *diagnostic = telemetry_.diagnostic;
      return DIGITOR_RESULT_UNSUPPORTED;
    }

    const PluginGpuFrame& output = index + 1 == request.instances.size()
        ? request.destination : request.intermediates[index];
    PluginZeroCopyRequest pass{};
    pass.instance = instance;
    pass.surface = request.surface;
    pass.project_or_clip_id = request.project_or_clip_id;
    pass.visual_stack_digest = request.visual_stack_digest;
    pass.input = input;
    pass.output = output;

    const DigitorResult result = frame_runtime_.process(pass, &local);
    if (result != DIGITOR_RESULT_OK) {
      ++telemetry_.failed_frames;
      telemetry_.diagnostic = local.empty()
          ? "plugin stack GPU pass failed without CPU fallback" : local;
      if (diagnostic) *diagnostic = telemetry_.diagnostic;
      return result;
    }
    ++telemetry_.plugin_dispatches;
    input = output;
  }

  ++telemetry_.stack_frames;
  if (request.surface == ConsumerPluginSurface::preview)
    ++telemetry_.preview_frames;
  else
    ++telemetry_.export_frames;
  telemetry_.diagnostic.clear();
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

PluginZeroCopyStackTelemetry PluginZeroCopyStackRuntime::telemetry() const {
  return telemetry_;
}

}  // namespace digitor
