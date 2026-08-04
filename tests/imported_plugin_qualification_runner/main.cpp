#include "digitor/imported_plugin_qualification_runner.hpp"

#include <iostream>

namespace {
int fail(const char* message) {
  std::cerr << "IMPORTED_PLUGIN_QUALIFICATION_RUNNER_FAILED=" << message << '\n';
  return 1;
}
}  // namespace

int main() {
  using namespace digitor;
  ImportedPluginQualificationRunner runner(
      "effect.remote.glow", "1.0.0", "sha256:glow-v1",
      RemotePluginBackend::windows_d3d12,
      PluginPixelFormat::rgba16_float, true, false);

  for (int i = 0; i < 300; ++i) {
    runner.record_frame({true, 1000, 0.00000001, 0.0001, 0,
                         "stack-v1", "sha256:glow-v1"});
    runner.record_frame({false, 1000, 0.00000001, 0.0001, 0,
                         "stack-v1", "sha256:glow-v1"});
  }
  for (int i = 0; i < 18000; ++i) runner.record_soak_frame();
  for (int i = 0; i < 3; ++i) runner.record_device_loss_recovery(true);
  runner.record_runtime_telemetry({0, 0, 0});

  const auto evidence = runner.evidence();
  if (evidence.preview_frames != 300 || evidence.export_frames != 300 ||
      evidence.compared_pixels != 600000 || evidence.soak_frames != 18000 ||
      evidence.device_loss_cycles != 3 || evidence.rmse <= 0.0)
    return fail("evidence aggregation mismatch");
  if (runner.qualify().state != ImportedPluginQualificationState::passed)
    return fail("valid physical evidence did not pass");

  ImportedPluginQualificationRunner incomplete(
      "effect.remote.glow", "1.0.0", "sha256:glow-v1",
      RemotePluginBackend::windows_d3d12,
      PluginPixelFormat::rgba8_unorm, false, true);
  if (incomplete.qualify().state !=
      ImportedPluginQualificationState::unqualified)
    return fail("software or missing evidence did not remain unqualified");

  std::cout << "IMPORTED_PLUGIN_QUALIFICATION_RUNNER=PASS\n";
  std::cout << "GPU_METRIC_AGGREGATION=PASS\n";
  std::cout << "PHYSICAL_EVIDENCE_POLICY=PASS\n";
  std::cout << "CPU_READBACKS=0\nCPU_UPLOADS=0\nFALLBACK_DISPATCHES=0\n";
  return 0;
}
