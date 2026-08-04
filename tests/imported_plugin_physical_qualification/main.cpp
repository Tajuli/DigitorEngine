#include "digitor/imported_plugin_physical_qualification.hpp"

#include <iostream>
#include <string>

int main() {
  using namespace digitor;

  ImportedPluginQualificationRunner runner(
      "effect.remote.glow", "1.0.0", "sha256:glow-v1",
      RemotePluginBackend::windows_d3d12,
      PluginPixelFormat::rgba16_float, true, false);

  ImportedPluginFrameEvidence preview_frame{};
  preview_frame.preview = true;
  preview_frame.compared_pixels = 4096;
  preview_frame.squared_error_sum = 0.00000001;
  preview_frame.max_absolute_error = 0.0001;
  preview_frame.alpha_mismatches = 0;
  preview_frame.visual_stack_digest = "stack:v1";
  preview_frame.package_identity = "sha256:glow-v1";

  auto export_frame = preview_frame;
  export_frame.preview = false;

  for (int i = 0; i < 300; ++i) {
    runner.record_frame(preview_frame);
    runner.record_frame(export_frame);
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
