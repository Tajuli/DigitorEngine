#include "digitor/effects_release_qualification.hpp"

#include <iostream>

int main() {
  using namespace digitor;

  EffectsQualificationEvidence complete{};
  complete.backend = EffectsQualificationBackend::windows_d3d12;
  complete.adapter_name = "qualification-adapter";
  complete.driver_version = "1.0";
  complete.shader_package_identity = "digitor.test.effects.v1";
  complete.visual_stack_digest = "sha256:test";
  complete.preview_frames = 300;
  complete.export_frames = 300;
  complete.soak_frames = 18000;
  complete.device_loss_cycles = 3;
  complete.sdr_rmse = 0.0;
  complete.hdr_rmse = 0.0;
  complete.alpha_max_error = 0.0;
  complete.physical_adapter = true;
  complete.hdr_tested = true;
  complete.device_loss_recovered = true;

  const auto pass = qualify_effects_release(complete);
  if (pass.state != EffectsQualificationState::passed ||
      !pass.failures.empty()) {
    std::cerr << "complete evidence did not pass\n";
    return 1;
  }

  auto software = complete;
  software.physical_adapter = false;
  const auto unqualified = qualify_effects_release(software);
  if (unqualified.state != EffectsQualificationState::unqualified) {
    std::cerr << "software-only evidence was not marked unqualified\n";
    return 1;
  }

  auto fallback = complete;
  fallback.fallback_dispatches = 1;
  const auto failed = qualify_effects_release(fallback);
  if (failed.state != EffectsQualificationState::failed ||
      failed.failures.empty()) {
    std::cerr << "silent fallback evidence did not fail\n";
    return 1;
  }

  auto mismatch = complete;
  mismatch.preview_export_mismatches = 1;
  if (qualify_effects_release(mismatch).state !=
      EffectsQualificationState::failed) {
    std::cerr << "preview/export mismatch did not fail\n";
    return 1;
  }

  std::cout << "QUALIFICATION_CONTRACT=PASS\n";
  std::cout << "BACKEND_COUNT=5\n";
  std::cout << "PHYSICAL_HARDWARE_REQUIRED=YES\n";
  return 0;
}
