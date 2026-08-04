#include "digitor/plugin_runtime_safety.hpp"

#include <string>

int main() {
  digitor::PluginRuntimeSafetyController safety;
  digitor::PluginRuntimeWorkload workload{};
  workload.plugin_id = "com.example.effect";
  workload.version = "1.0.0";
  workload.package_sha256 = std::string(64, 'a');
  workload.kind = digitor::PluginRuntimeKind::effect;
  workload.surface = digitor::PluginRuntimeSurface::preview;
  workload.pass_count = 2;
  workload.dispatch_invocations = 1920ull * 1080ull;
  workload.parameter_count = 4;
  digitor::PluginRuntimeDiagnostic diagnostic{};
  return safety.validate(workload, diagnostic) ? 0 : 1;
}
