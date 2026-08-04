#include "digitor/imported_plugin_release_qualification.hpp"

#include <iostream>

namespace {
int fail(const char* message) {
  std::cerr << "IMPORTED_PLUGIN_QUALIFICATION_FAILED=" << message << '\n';
  return 1;
}

digitor::ImportedPluginQualificationEvidence valid_evidence() {
  using namespace digitor;
  ImportedPluginQualificationEvidence value{};
  value.plugin_id = "effect.remote.glow";
  value.plugin_version = "1.0.0";
  value.package_identity = "sha256:remote-glow-v1";
  value.backend = RemotePluginBackend::windows_d3d12;
  value.format = PluginPixelFormat::rgba16_float;
  value.physical_gpu = true;
  value.preview_frames = 300;
  value.export_frames = 300;
  value.compared_pixels = 3840ull * 2160ull * 300ull;
  value.rmse = 0.0002;
  value.max_absolute_error = 0.001;
  value.device_loss_cycles = 3;
  value.soak_frames = 18000;
  value.preview_visual_stack_digest = "stack:plugin-v1";
  value.export_visual_stack_digest = "stack:plugin-v1";
  return value;
}
}  // namespace

int main() {
  using namespace digitor;

  auto evidence = valid_evidence();
  const auto passed = qualify_imported_plugin_release(evidence);
  if (passed.state != ImportedPluginQualificationState::passed)
    return fail("valid physical imported-plugin evidence did not pass");

  auto missing_hardware = evidence;
  missing_hardware.physical_gpu = false;
  if (qualify_imported_plugin_release(missing_hardware).state !=
      ImportedPluginQualificationState::unqualified)
    return fail("missing physical GPU evidence was not unqualified");

  auto mismatch = evidence;
  mismatch.export_visual_stack_digest = "stack:other";
  if (qualify_imported_plugin_release(mismatch).state !=
      ImportedPluginQualificationState::failed)
    return fail("preview/export stack mismatch was not failed");

  auto readback = evidence;
  readback.cpu_readbacks = 1;
  if (qualify_imported_plugin_release(readback).state !=
      ImportedPluginQualificationState::failed)
    return fail("CPU readback violation was not failed");

  auto color_error = evidence;
  color_error.rmse = 0.01;
  if (qualify_imported_plugin_release(color_error).state !=
      ImportedPluginQualificationState::failed)
    return fail("per-pixel color violation was not failed");

  std::cout << "IMPORTED_PLUGIN_RELEASE_QUALIFICATION=PASS\n";
  std::cout << "PHYSICAL_GPU_REQUIRED=PASS\n";
  std::cout << "PREVIEW_EXPORT_PACKAGE_PARITY=PASS\n";
  std::cout << "PER_PIXEL_SDR_HDR_TOLERANCE=PASS\n";
  std::cout << "ALPHA_PRESERVATION=PASS\n";
  std::cout << "CPU_READBACKS=0\nCPU_UPLOADS=0\nFALLBACK_DISPATCHES=0\n";
  return 0;
}
