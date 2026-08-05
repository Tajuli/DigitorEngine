#include "digitor/production_evidence_capture.hpp"

#include <cmath>
#include <cstdint>

namespace {

int require(bool condition, int code) {
  return condition ? 0 : code;
}

}  // namespace

int main() {
  digitor::EvidenceCaptureConfig config;
  config.platform = digitor::CapturePlatform::windows;
  config.backend = digitor::CaptureBackend::d3d12;
  config.device_id = "windows-device-1";
  config.gpu_name = "qualification-gpu";
  config.driver_version = "1.0";
  config.engine_version = "5.0.0";
  config.source_commit = "abcdef123456";
  config.warmup_frames = 2;
  config.minimum_frames = 5;
  config.minimum_soak_seconds = 10;

  digitor::ProductionEvidenceCapture capture(config);
  if (const int code = require(
          capture.result().status == digitor::CaptureStatus::collecting, 1)) {
    return code;
  }

  for (std::uint64_t index = 0; index < 7; ++index) {
    digitor::EvidenceFrameSample sample;
    sample.frame_index = index;
    sample.frame_time_ms = 10.0 + static_cast<double>(index);
    sample.preview_digest = 55u;
    sample.export_digest = 55u;
    sample.gpu_execution_observed = true;
    if (capture.add_sample(sample) == digitor::CaptureStatus::failed) {
      return 2;
    }
  }
  if (capture.finalize(12u) != digitor::CaptureStatus::complete) {
    return 3;
  }
  const auto result = capture.result();
  if (const int code = require(result.accepted_frames == 5u, 4)) {
    return code;
  }
  if (const int code = require(result.preview_digest == result.export_digest,
                               5)) {
    return code;
  }
  if (const int code = require(result.digest != 0u, 6)) {
    return code;
  }
  if (const int code = require(result.average_frame_ms > 0.0 &&
                                   result.p95_frame_ms >=
                                       result.average_frame_ms,
                               7)) {
    return code;
  }

  digitor::ProductionEvidenceCapture mismatch(config);
  for (std::uint64_t index = 0; index < 3; ++index) {
    digitor::EvidenceFrameSample sample;
    sample.frame_index = index;
    sample.frame_time_ms = 12.0;
    sample.preview_digest = 100u;
    sample.export_digest = index == 2u ? 101u : 100u;
    sample.gpu_execution_observed = true;
    mismatch.add_sample(sample);
  }
  if (const int code = require(
          mismatch.result().status == digitor::CaptureStatus::failed, 8)) {
    return code;
  }

  DigitorEvidenceCaptureConfig ffi_config{};
  ffi_config.platform = 2u;
  ffi_config.backend = 4u;
  ffi_config.device_id = "android-device";
  ffi_config.gpu_name = "gles-gpu";
  ffi_config.driver_version = "2.0";
  ffi_config.engine_version = "5.0.0";
  ffi_config.source_commit = "abcdef123456";
  ffi_config.minimum_frames = 1u;
  ffi_config.minimum_soak_seconds = 1u;
  ffi_config.require_preview_export_parity = 1u;
  ffi_config.require_gpu_observation = 1u;
  const auto handle = digitor_evidence_capture_create(&ffi_config);
  if (!handle) {
    return 9;
  }
  DigitorEvidenceFrameSample ffi_sample{};
  ffi_sample.frame_index = 1u;
  ffi_sample.frame_time_ms = 16.0;
  ffi_sample.preview_digest = 7u;
  ffi_sample.export_digest = 7u;
  ffi_sample.gpu_execution_observed = 1u;
  if (digitor_evidence_capture_add_sample(handle, &ffi_sample) != 1u ||
      digitor_evidence_capture_finalize(handle, 1u) != 2u) {
    digitor_evidence_capture_destroy(handle);
    return 10;
  }
  DigitorEvidenceCaptureResult ffi_result{};
  const auto ffi_status = digitor_evidence_capture_result(handle, &ffi_result);
  digitor_evidence_capture_destroy(handle);
  if (ffi_status != 0u || ffi_result.status != 2u || ffi_result.digest == 0u) {
    return 11;
  }
  return 0;
}
