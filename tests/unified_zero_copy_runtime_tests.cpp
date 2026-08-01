#include "digitor/unified_zero_copy_runtime.hpp"

#include <cassert>
#include <cstdint>
#include <string>

using namespace digitor;

static UnifiedZeroCopyConfig config() {
  return {ZeroCopyPlatform::windows, "gpu-1", "driver-1", "commit-1", true, true, 2};
}

static ZeroCopyQualificationEvidence evidence() {
  ZeroCopyQualificationEvidence e{};
  e.platform = ZeroCopyPlatform::windows;
  e.device_identity = "gpu-1";
  e.driver_or_os_build = "driver-1";
  e.engine_commit = "commit-1";
  e.qualification_id = "qualification-1";
  e.valid_until_unix_seconds = 2000;
  e.strict_gpu_first = true;
  e.decode_zero_copy = true;
  e.render_zero_copy = true;
  e.preview_export_identity = true;
  e.per_pixel_accuracy = true;
  e.hardware_encode = true;
  e.sustained_4k = true;
  e.stress_and_leak = true;
  e.measured_fps = 60.0;
  e.minimum_fps = 30.0;
  e.max_mean_error = 0.0001;
  e.allowed_mean_error = 0.001;
  e.resource_delta = 0;
  e.allowed_resource_delta = 0;
  return e;
}

int main() {
  std::string diagnostic;
  auto e = evidence();
  assert(validate_zero_copy_evidence(config(), e, 1000, diagnostic) == DIGITOR_RESULT_OK);

  auto expired = e;
  expired.valid_until_unix_seconds = 1000;
  assert(validate_zero_copy_evidence(config(), expired, 1000, diagnostic) != DIGITOR_RESULT_OK);

  auto wrong_device = e;
  wrong_device.device_identity = "gpu-2";
  assert(validate_zero_copy_evidence(config(), wrong_device, 1000, diagnostic) != DIGITOR_RESULT_OK);

  auto slow = e;
  slow.measured_fps = 29.0;
  assert(validate_zero_copy_evidence(config(), slow, 1000, diagnostic) != DIGITOR_RESULT_OK);

  auto inaccurate = e;
  inaccurate.max_mean_error = 0.01;
  assert(validate_zero_copy_evidence(config(), inaccurate, 1000, diagnostic) != DIGITOR_RESULT_OK);

  auto leaking = e;
  leaking.resource_delta = 1;
  assert(validate_zero_copy_evidence(config(), leaking, 1000, diagnostic) != DIGITOR_RESULT_OK);

  std::uint64_t previews = 0;
  std::uint64_t exports = 0;
  std::uint64_t shared = 0;
  std::uint64_t resets = 0;
  UnifiedZeroCopyBinding binding{};
  binding.preview = [&](std::int64_t) { ++previews; return DIGITOR_RESULT_OK; };
  binding.export_frame = [&](std::int64_t) { ++exports; return DIGITOR_RESULT_OK; };
  binding.preview_and_export = [&](std::int64_t) { ++shared; return DIGITOR_RESULT_OK; };
  binding.reset_platform_quarantine = [&]() { ++resets; return DIGITOR_RESULT_OK; };

  UnifiedZeroCopyRuntime runtime(config(), binding);
  assert(runtime.activate(e, 1000) == DIGITOR_RESULT_OK);
  assert(runtime.production_ready());
  assert(runtime.preview(10) == DIGITOR_RESULT_OK);
  assert(runtime.export_frame(20) == DIGITOR_RESULT_OK);
  assert(runtime.preview_and_export(30) == DIGITOR_RESULT_OK);
  auto t = runtime.telemetry();
  assert(t.preview_frames == 2);
  assert(t.export_frames == 2);
  assert(t.shared_frame_reuses == 1);
  assert(t.cpu_copies == 0 && t.cpu_fallback_frames == 0);

  UnifiedZeroCopyBinding failing{};
  failing.preview = [](std::int64_t) { return DIGITOR_RESULT_BACKEND_UNAVAILABLE; };
  failing.export_frame = failing.preview;
  failing.preview_and_export = failing.preview;
  failing.reset_platform_quarantine = [] { return DIGITOR_RESULT_OK; };
  UnifiedZeroCopyRuntime quarantined(config(), failing);
  assert(quarantined.activate(e, 1000) == DIGITOR_RESULT_OK);
  assert(quarantined.preview(1) != DIGITOR_RESULT_OK);
  assert(quarantined.preview(2) != DIGITOR_RESULT_OK);
  assert(quarantined.telemetry().state == ZeroCopyRuntimeState::quarantined);
  assert(!quarantined.production_ready());
  assert(quarantined.reset_quarantine(e, 1000) == DIGITOR_RESULT_OK);
  assert(quarantined.telemetry().state == ZeroCopyRuntimeState::active);

  assert(previews == 1 && exports == 1 && shared == 1 && resets == 0);
  return 0;
}
