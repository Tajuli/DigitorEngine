#include "digitor/plugin_native_pass_dispatch.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace digitor {
namespace {

bool same_frame_contract(const PluginGpuFrame& a,
                         const PluginGpuFrame& b) noexcept {
  return a.backend == b.backend && a.width == b.width &&
         a.height == b.height && a.format == b.format &&
         a.primaries == b.primaries && a.transfer == b.transfer &&
         a.range == b.range && a.alpha == b.alpha &&
         a.timestamp_us == b.timestamp_us;
}

std::uint32_t ceil_div(std::uint32_t value, std::uint32_t divisor) noexcept {
  return divisor == 0 ? 0 : (value + divisor - 1u) / divisor;
}

PluginNativeTextureBinding texture_binding(const PluginGpuFrame& frame) {
  PluginNativeTextureBinding out{};
  out.native_texture_handle = frame.native_texture_handle;
  out.synchronization_handle = frame.synchronization_handle;
  out.synchronization_value = frame.synchronization_value;
  out.width = frame.width;
  out.height = frame.height;
  out.format = frame.format;
  return out;
}

}  // namespace

PluginNativePassDispatcher::PluginNativePassDispatcher(
    const PluginBackendPackageLoader& loader,
    PluginNativePassDispatchBindings bindings)
    : loader_(loader), bindings_(std::move(bindings)) {}

DigitorResult PluginNativePassDispatcher::record(
    const PluginGpuDispatchPass& pass,
    std::string* diagnostic) noexcept {
  auto fail = [&](DigitorResult result, std::string message) {
    ++telemetry_.failed_passes;
    telemetry_.diagnostic = std::move(message);
    if (diagnostic) *diagnostic = telemetry_.diagnostic;
    return result;
  };

  if (bindings_.device_identity == 0 || !bindings_.record_dispatch) {
    return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                "plugin native pass bindings are incomplete");
  }
  if (pass.program.backend != bindings_.selected_backend ||
      pass.input.backend != bindings_.selected_backend ||
      pass.output.backend != bindings_.selected_backend) {
    return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                "plugin native pass differs from selected backend");
  }
  if (pass.input.native_texture_handle == 0 ||
      pass.output.native_texture_handle == 0 ||
      pass.input.native_texture_handle == pass.output.native_texture_handle ||
      !same_frame_contract(pass.input, pass.output)) {
    return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                "plugin native pass texture contract is invalid");
  }
  if (pass.pass_index >= pass.program.passes.size() ||
      pass.pass.entry_point.empty() || pass.pass.shader_asset.empty() ||
      pass.pass.workgroup_x == 0 || pass.pass.workgroup_y == 0 ||
      pass.pass.workgroup_z == 0 || !pass.pass.preserves_alpha ||
      !pass.pass.deterministic) {
    return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                "plugin native pass descriptor is invalid");
  }

  const auto* pipeline = loader_.pipeline(
      pass.program.plugin_id, pass.program.plugin_version,
      plugin_program_format(pass.input.format));
  if (!pipeline || pipeline->native_pipeline_handle == 0 ||
      pipeline->device_identity != bindings_.device_identity ||
      pipeline->backend != bindings_.selected_backend ||
      pipeline->package_identity != pass.program.package_identity ||
      pipeline->plugin_id != pass.program.plugin_id ||
      pipeline->plugin_version != pass.program.plugin_version ||
      pipeline->format != pass.program.format) {
    return fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                "plugin native pipeline identity is unavailable or mismatched");
  }

  const std::uint32_t groups_x = ceil_div(pass.input.width,
                                          pass.pass.workgroup_x);
  const std::uint32_t groups_y = ceil_div(pass.input.height,
                                          pass.pass.workgroup_y);
  const std::uint32_t groups_z = ceil_div(1u, pass.pass.workgroup_z);
  if (groups_x == 0 || groups_y == 0 || groups_z == 0 ||
      groups_x > 65535u || groups_y > 65535u || groups_z > 65535u) {
    return fail(DIGITOR_RESULT_UNSUPPORTED,
                "plugin native dispatch geometry exceeds backend limits");
  }

  PluginNativeDispatch native{};
  native.pipeline = *pipeline;
  native.pass = pass.pass;
  native.pass_index = pass.pass_index;
  native.input = texture_binding(pass.input);
  native.output = texture_binding(pass.output);
  native.group_count_x = groups_x;
  native.group_count_y = groups_y;
  native.group_count_z = groups_z;
  native.parameters = pass.parameters;

  std::string local;
  const DigitorResult recorded = bindings_.record_dispatch(native, local);
  if (recorded != DIGITOR_RESULT_OK) {
    return fail(recorded,
                local.empty()
                    ? "plugin native pass recording failed without fallback"
                    : local);
  }

  ++telemetry_.recorded_passes;
  telemetry_.diagnostic.clear();
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

PluginNativePassDispatchTelemetry PluginNativePassDispatcher::telemetry() const {
  return telemetry_;
}

PluginGpuRecordPass make_plugin_native_record_pass(
    PluginNativePassDispatcher& dispatcher) {
  return [&dispatcher](const PluginGpuDispatchPass& pass,
                       std::string& diagnostic) {
    return dispatcher.record(pass, &diagnostic);
  };
}

}  // namespace digitor
