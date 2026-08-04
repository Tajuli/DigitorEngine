#include "digitor/imported_plugin_physical_qualification.hpp"

#include <iostream>
#include <string>

int main() {
  using namespace digitor;

  ImportedPluginQualificationRunner runner(
      "effect.remote.glow", "1.0.0", "sha256:glow-v1",
      RemotePluginBackend::windows_d3d12,
      PluginPixelFormat::rgba16_float, true, false);

  for (int i = 0; i < 300; ++i) {
    runner.record_frame({true, "sha256:glow-v1", "stack:v1",
                         4096, 0.00000001, 0.0001, 0});
    runner.record_frame({false, "sha256:glow-v1", "stack:v1",
                         4096, 0.00000001, 0.0001, 0});
  }
  for (int i = 0; i < 18000; ++i) runner.record_soak_frame();
  runner.record_device_loss_recovery(true);
  runner.record_device_loss_recovery(true);
  runner.record_device_loss_recovery(true);
  runner.record_runtime_telemetry({0, 0, 0});

  ImportedPluginPhysicalQualificationReport report{};
  report.metadata = {"5.0.0", "Windows 11", "Physical GPU",
                     "driver-1", "run-001"};
  report.evidence = runner.evidence();
  report.result = runner.qualify();

  std::string diagnostic;
  if (!imported_plugin_physical_report_is_releasable(report, &diagnostic)) {
    std::cerr << "PHYSICAL_REPORT_FAILED=" << diagnostic << '\n';
    return 1;
  }

  const auto serialized = serialize_imported_plugin_physical_report(report);
  if (serialized.find("state=PASSED") == std::string::npos ||
      serialized.find("cpu_readbacks=0") == std::string::npos ||
      serialized.find("fallback_dispatches=0") == std::string::npos) {
    std::cerr << "PHYSICAL_REPORT_FAILED=serialized evidence mismatch\n";
    return 1;
  }

  auto unqualified = report;
  unqualified.evidence.physical_gpu = false;
  unqualified.result = qualify_imported_plugin_release(unqualified.evidence);
  if (imported_plugin_physical_report_is_releasable(unqualified, &diagnostic)) {
    std::cerr << "PHYSICAL_REPORT_FAILED=hosted evidence was accepted\n";
    return 1;
  }

  std::cout << "IMPORTED_PLUGIN_PHYSICAL_REPORT=PASS\n";
  std::cout << "HOSTED_RUNNER_CANNOT_QUALIFY=PASS\n";
  std::cout << "ZERO_COPY_EVIDENCE=PASS\n";
  return 0;
}
