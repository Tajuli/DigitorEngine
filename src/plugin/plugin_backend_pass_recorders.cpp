#include "digitor/plugin_backend_pass_recorders.hpp"

#include <utility>

namespace digitor {
namespace {

bool valid_dispatch(const PluginNativeDispatch& value,
                    const PluginBackendPassRecorderBindings& bindings,
                    std::string& diagnostic) noexcept {
  if (value.pipeline.native_pipeline_handle == 0 ||
      value.pipeline.device_identity != bindings.device_identity ||
      value.pipeline.backend != bindings.selected_backend ||
      value.input.native_texture_handle == 0 ||
      value.output.native_texture_handle == 0 ||
      value.input.native_texture_handle == value.output.native_texture_handle ||
      value.group_count_x == 0 || value.group_count_y == 0 ||
      value.group_count_z == 0) {
    diagnostic = "plugin backend native dispatch identity or geometry is invalid";
    return false;
  }
  if (value.input.backend != bindings.selected_backend ||
      value.output.backend != bindings.selected_backend ||
      value.input.format != value.output.format ||
      value.input.width != value.output.width ||
      value.input.height != value.output.height ||
      value.input.synchronization_handle == 0 ||
      value.output.synchronization_handle == 0) {
    diagnostic = "plugin backend texture or synchronization contract is invalid";
    return false;
  }
  const auto& context = bindings.command_context;
  if (!context.provider_owned || !context.external_synchronization ||
      context.command_context_handle == 0 ||
      context.device_identity != bindings.device_identity ||
      context.backend != bindings.selected_backend) {
    diagnostic = "plugin backend command context is not provider-owned";
    return false;
  }
  diagnostic.clear();
  return true;
}

}  // namespace

PluginBackendPassRecorder::PluginBackendPassRecorder(
    PluginBackendPassRecorderBindings bindings)
    : bindings_(std::move(bindings)) {}

DigitorResult PluginBackendPassRecorder::record(
    const PluginNativeDispatch& dispatch,
    std::string* diagnostic) noexcept {
  std::string local;
  auto fail = [&](DigitorResult result, std::string message) {
    ++telemetry_.failed_passes;
    telemetry_.diagnostic = std::move(message);
    if (diagnostic) *diagnostic = telemetry_.diagnostic;
    return result;
  };

  if (bindings_.device_identity == 0 ||
      !valid_dispatch(dispatch, bindings_, local)) {
    return fail(DIGITOR_RESULT_INVALID_ARGUMENT, local.empty()
        ? "plugin backend recorder bindings are invalid" : local);
  }

  DigitorResult result = DIGITOR_RESULT_UNSUPPORTED;
  switch (bindings_.selected_backend) {
    case RemotePluginBackend::windows_d3d12:
      if (!bindings_.record_d3d12)
        return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                    "D3D12 plugin pass recorder is unavailable");
      result = bindings_.record_d3d12(dispatch, bindings_.command_context, local);
      break;
    case RemotePluginBackend::windows_vulkan:
    case RemotePluginBackend::android_vulkan:
      if (!bindings_.record_vulkan)
        return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                    "Vulkan plugin pass recorder is unavailable");
      result = bindings_.record_vulkan(dispatch, bindings_.command_context, local);
      break;
    case RemotePluginBackend::apple_metal:
      if (!bindings_.record_metal)
        return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                    "Metal plugin pass recorder is unavailable");
      result = bindings_.record_metal(dispatch, bindings_.command_context, local);
      break;
    case RemotePluginBackend::android_gles:
      if (!bindings_.record_gles)
        return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                    "GLES plugin pass recorder is unavailable");
      result = bindings_.record_gles(dispatch, bindings_.command_context, local);
      break;
  }

  if (result != DIGITOR_RESULT_OK) {
    return fail(result, local.empty()
        ? "plugin backend pass recording failed without fallback" : local);
  }

  ++telemetry_.recorded_passes;
  telemetry_.descriptor_or_resource_bindings += 3;
  telemetry_.synchronization_bindings += 2;
  telemetry_.diagnostic.clear();
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

PluginBackendPassRecorderTelemetry PluginBackendPassRecorder::telemetry() const {
  return telemetry_;
}

PluginNativeRecordDispatch make_plugin_backend_record_callback(
    PluginBackendPassRecorder& recorder) {
  return [&recorder](const PluginNativeDispatch& dispatch,
                     std::string& diagnostic) {
    return recorder.record(dispatch, &diagnostic);
  };
}

}  // namespace digitor
