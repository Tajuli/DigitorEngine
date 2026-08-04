#include "digitor/plugin_gpu_multipass.hpp"

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

void release_all(const PluginGpuReleaseIntermediate& release,
                 std::vector<PluginGpuFrame>& frames) noexcept {
  if (release) {
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) release(*it);
  }
  frames.clear();
}

}  // namespace

PluginGpuMultiPassRuntime::PluginGpuMultiPassRuntime(
    const PluginGpuProgramRegistry& registry,
    PluginGpuMultiPassBindings bindings)
    : registry_(registry), bindings_(std::move(bindings)) {}

DigitorResult PluginGpuMultiPassRuntime::dispatch(
    const PluginZeroCopyRequest& request,
    std::string* diagnostic) noexcept {
  std::string local;
  auto fail = [&](DigitorResult result, std::string message) {
    ++telemetry_.failed_programs;
    telemetry_.diagnostic = std::move(message);
    if (diagnostic) *diagnostic = telemetry_.diagnostic;
    return result;
  };

  if (request.input.backend != bindings_.selected_backend ||
      request.output.backend != bindings_.selected_backend ||
      request.input.native_texture_handle == 0 ||
      request.output.native_texture_handle == 0 ||
      !same_frame_contract(request.input, request.output)) {
    return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                "plugin multi-pass input/output GPU frame contract is invalid");
  }
  if (!bindings_.allocate_intermediate || !bindings_.release_intermediate ||
      !bindings_.record_pass || !bindings_.submit) {
    return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                "plugin multi-pass backend bindings are incomplete");
  }

  const auto program = registry_.resolve(
      request.instance.plugin_id, request.instance.plugin_version,
      bindings_.selected_backend, plugin_program_format(request.input.format));
  if (!program) {
    return fail(DIGITOR_RESULT_UNSUPPORTED,
                "compatible plugin GPU program is not registered");
  }
  if (program->passes.empty() || program->passes.size() > 32) {
    return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                "plugin GPU pass count is invalid");
  }

  std::vector<PluginGpuFrame> intermediates;
  intermediates.reserve(program->passes.size() - 1);
  PluginGpuFrame current = request.input;

  for (std::size_t index = 0; index < program->passes.size(); ++index) {
    const bool final_pass = index + 1 == program->passes.size();
    PluginGpuFrame target{};
    if (final_pass) {
      target = request.output;
    } else {
      target = request.input;
      target.native_texture_handle = 0;
      target.synchronization_handle = 0;
      target.synchronization_value = 0;
      const DigitorResult allocation =
          bindings_.allocate_intermediate(request.input, target, local);
      if (allocation != DIGITOR_RESULT_OK ||
          target.native_texture_handle == 0 ||
          !same_frame_contract(request.input, target) ||
          target.native_texture_handle == current.native_texture_handle ||
          target.native_texture_handle == request.output.native_texture_handle) {
        release_all(bindings_.release_intermediate, intermediates);
        return fail(allocation == DIGITOR_RESULT_OK
                        ? DIGITOR_RESULT_BACKEND_UNAVAILABLE
                        : allocation,
                    local.empty()
                        ? "plugin GPU intermediate allocation violated zero-copy contract"
                        : local);
      }
      intermediates.push_back(target);
      ++telemetry_.intermediate_allocations;
    }

    PluginGpuDispatchPass pass{};
    pass.program = *program;
    pass.pass = program->passes[index];
    pass.pass_index = static_cast<std::uint32_t>(index);
    pass.input = current;
    pass.output = target;
    pass.parameters = request.instance.parameters;

    const DigitorResult recorded = bindings_.record_pass(pass, local);
    if (recorded != DIGITOR_RESULT_OK) {
      release_all(bindings_.release_intermediate, intermediates);
      return fail(recorded,
                  local.empty()
                      ? "plugin GPU pass recording failed without fallback"
                      : local);
    }
    ++telemetry_.recorded_passes;
    current = target;
  }

  const DigitorResult submitted = bindings_.submit(local);
  if (submitted != DIGITOR_RESULT_OK) {
    release_all(bindings_.release_intermediate, intermediates);
    return fail(submitted,
                local.empty()
                    ? "plugin GPU submission failed without fallback"
                    : local);
  }

  ++telemetry_.submissions;
  ++telemetry_.dispatched_programs;
  release_all(bindings_.release_intermediate, intermediates);
  telemetry_.diagnostic.clear();
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

PluginGpuMultiPassTelemetry PluginGpuMultiPassRuntime::telemetry() const {
  return telemetry_;
}

}  // namespace digitor
