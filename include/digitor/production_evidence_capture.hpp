#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class CaptureStatus : std::uint32_t {
  invalid = 0,
  collecting = 1,
  complete = 2,
  failed = 3,
};

enum class CapturePlatform : std::uint32_t {
  windows = 1,
  android = 2,
  macos = 3,
  ios = 4,
};

enum class CaptureBackend : std::uint32_t {
  vulkan = 1,
  d3d12 = 2,
  metal = 3,
  gles = 4,
};

struct EvidenceCaptureConfig {
  CapturePlatform platform{};
  CaptureBackend backend{};
  std::string device_id;
  std::string gpu_name;
  std::string driver_version;
  std::string engine_version;
  std::string source_commit;
  std::uint64_t warmup_frames{30};
  std::uint64_t minimum_frames{300};
  std::uint64_t minimum_soak_seconds{60};
  bool require_preview_export_parity{true};
  bool require_gpu_observation{true};
};

struct EvidenceFrameSample {
  std::uint64_t frame_index{0};
  double frame_time_ms{0.0};
  std::uint64_t preview_digest{0};
  std::uint64_t export_digest{0};
  bool dropped{false};
  bool validation_error{false};
  bool device_loss{false};
  bool gpu_execution_observed{false};
  bool silent_cpu_fallback_observed{false};
};

struct EvidenceCaptureResult {
  CaptureStatus status{CaptureStatus::invalid};
  std::uint64_t accepted_frames{0};
  std::uint64_t dropped_frames{0};
  std::uint64_t validation_errors{0};
  std::uint64_t device_loss_events{0};
  std::uint64_t soak_seconds{0};
  double average_frame_ms{0.0};
  double p95_frame_ms{0.0};
  std::uint64_t preview_digest{0};
  std::uint64_t export_digest{0};
  bool gpu_execution_observed{false};
  bool silent_cpu_fallback_observed{false};
  std::uint64_t digest{0};
};

class ProductionEvidenceCapture {
 public:
  explicit ProductionEvidenceCapture(EvidenceCaptureConfig config);
  CaptureStatus add_sample(const EvidenceFrameSample& sample) noexcept;
  CaptureStatus finalize(std::uint64_t elapsed_seconds) noexcept;
  const EvidenceCaptureResult& result() const noexcept;
  const EvidenceCaptureConfig& config() const noexcept;

 private:
  EvidenceCaptureConfig config_;
  EvidenceCaptureResult result_;
  std::vector<double> frame_times_;
  std::uint64_t seen_frames_{0};
  std::uint64_t last_frame_index_{0};
  bool has_frame_{false};
};

}  // namespace digitor

extern "C" {

typedef void* DigitorEvidenceCaptureHandle;

struct DigitorEvidenceCaptureConfig {
  std::uint32_t platform;
  std::uint32_t backend;
  const char* device_id;
  const char* gpu_name;
  const char* driver_version;
  const char* engine_version;
  const char* source_commit;
  std::uint64_t warmup_frames;
  std::uint64_t minimum_frames;
  std::uint64_t minimum_soak_seconds;
  std::uint32_t require_preview_export_parity;
  std::uint32_t require_gpu_observation;
};

struct DigitorEvidenceFrameSample {
  std::uint64_t frame_index;
  double frame_time_ms;
  std::uint64_t preview_digest;
  std::uint64_t export_digest;
  std::uint32_t dropped;
  std::uint32_t validation_error;
  std::uint32_t device_loss;
  std::uint32_t gpu_execution_observed;
  std::uint32_t silent_cpu_fallback_observed;
};

struct DigitorEvidenceCaptureResult {
  std::uint32_t status;
  std::uint64_t accepted_frames;
  std::uint64_t dropped_frames;
  std::uint64_t validation_errors;
  std::uint64_t device_loss_events;
  std::uint64_t soak_seconds;
  double average_frame_ms;
  double p95_frame_ms;
  std::uint64_t preview_digest;
  std::uint64_t export_digest;
  std::uint32_t gpu_execution_observed;
  std::uint32_t silent_cpu_fallback_observed;
  std::uint64_t digest;
};

DigitorEvidenceCaptureHandle digitor_evidence_capture_create(
    const DigitorEvidenceCaptureConfig* config);
void digitor_evidence_capture_destroy(DigitorEvidenceCaptureHandle handle);
std::uint32_t digitor_evidence_capture_add_sample(
    DigitorEvidenceCaptureHandle handle,
    const DigitorEvidenceFrameSample* sample);
std::uint32_t digitor_evidence_capture_finalize(
    DigitorEvidenceCaptureHandle handle, std::uint64_t elapsed_seconds);
std::uint32_t digitor_evidence_capture_result(
    DigitorEvidenceCaptureHandle handle,
    DigitorEvidenceCaptureResult* output);

}
