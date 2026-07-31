#include "digitor/windows_zero_copy_production.hpp"

#include <cassert>
#include <string>

namespace {
digitor::WindowsZeroCopyProductionConfig valid_config() {
  using namespace digitor;
  WindowsZeroCopyProductionConfig c{};
  c.current_adapter_luid = 44;
  c.current_driver_version = 55;
  c.current_engine_commit = "commit-a";
  c.current_unix_seconds = 1000;
  auto& e = c.evidence;
  e.production_ready = true;
  e.strict_gpu_first = true;
  e.nv12_pass = true;
  e.p010_pass = true;
  e.preview_export_identity_pass = true;
  e.per_pixel_accuracy_pass = true;
  e.sustained_4k_pass = true;
  e.leak_test_pass = true;
  e.hevc_main10_pass = true;
  e.adapter_luid = 44;
  e.driver_version = 55;
  e.engine_commit = "commit-a";
  e.qualification_id = "qualification-a";
  e.expires_unix_seconds = 2000;
  e.minimum_fps = 30.0;
  e.measured_fps = 60.0;
  e.maximum_mean_error = 0.001;
  e.measured_mean_error = 0.0001;
  e.maximum_resource_delta = 2;
  e.measured_resource_delta = 0;
  return c;
}
}

int main() {
  using namespace digitor;
  std::string diagnostic;
  auto c = valid_config();
  assert(WindowsZeroCopyProductionPipeline::validate_evidence(c, diagnostic) == DIGITOR_RESULT_OK);
  assert(!diagnostic.empty());

  auto invalid = c;
  invalid.evidence.production_ready = false;
  assert(WindowsZeroCopyProductionPipeline::validate_evidence(invalid, diagnostic) != DIGITOR_RESULT_OK);

  invalid = c;
  invalid.evidence.adapter_luid++;
  assert(WindowsZeroCopyProductionPipeline::validate_evidence(invalid, diagnostic) != DIGITOR_RESULT_OK);

  invalid = c;
  invalid.evidence.driver_version++;
  assert(WindowsZeroCopyProductionPipeline::validate_evidence(invalid, diagnostic) != DIGITOR_RESULT_OK);

  invalid = c;
  invalid.evidence.engine_commit = "other";
  assert(WindowsZeroCopyProductionPipeline::validate_evidence(invalid, diagnostic) != DIGITOR_RESULT_OK);

  invalid = c;
  invalid.evidence.expires_unix_seconds = invalid.current_unix_seconds;
  assert(WindowsZeroCopyProductionPipeline::validate_evidence(invalid, diagnostic) != DIGITOR_RESULT_OK);

  invalid = c;
  invalid.evidence.measured_fps = 29.0;
  assert(WindowsZeroCopyProductionPipeline::validate_evidence(invalid, diagnostic) != DIGITOR_RESULT_OK);

  invalid = c;
  invalid.evidence.measured_mean_error = 0.01;
  assert(WindowsZeroCopyProductionPipeline::validate_evidence(invalid, diagnostic) != DIGITOR_RESULT_OK);

  invalid = c;
  invalid.evidence.measured_resource_delta = 3;
  assert(WindowsZeroCopyProductionPipeline::validate_evidence(invalid, diagnostic) != DIGITOR_RESULT_OK);

  invalid = c;
  invalid.evidence.hevc_main10_pass = false;
  assert(WindowsZeroCopyProductionPipeline::validate_evidence(invalid, diagnostic) != DIGITOR_RESULT_OK);
  return 0;
}
