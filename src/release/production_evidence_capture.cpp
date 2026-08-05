#include "digitor/production_evidence_capture.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <new>

namespace digitor {
namespace {

std::uint64_t append(std::uint64_t hash, const void* data,
                     std::size_t size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ull;
  }
  return hash;
}

bool valid_platform_backend(CapturePlatform platform,
                            CaptureBackend backend) noexcept {
  switch (platform) {
    case CapturePlatform::windows:
      return backend == CaptureBackend::vulkan ||
             backend == CaptureBackend::d3d12;
    case CapturePlatform::android:
      return backend == CaptureBackend::vulkan ||
             backend == CaptureBackend::gles;
    case CapturePlatform::macos:
    case CapturePlatform::ios:
      return backend == CaptureBackend::metal;
  }
  return false;
}

bool valid_config(const EvidenceCaptureConfig& config) noexcept {
  return valid_platform_backend(config.platform, config.backend) &&
         !config.device_id.empty() && !config.gpu_name.empty() &&
         !config.driver_version.empty() && !config.engine_version.empty() &&
         config.source_commit.size() >= 7u && config.minimum_frames > 0u &&
         config.minimum_soak_seconds > 0u;
}

}  // namespace

ProductionEvidenceCapture::ProductionEvidenceCapture(
    EvidenceCaptureConfig config)
    : config_(std::move(config)) {
  result_.status = valid_config(config_) ? CaptureStatus::collecting
                                         : CaptureStatus::invalid;
}

CaptureStatus ProductionEvidenceCapture::add_sample(
    const EvidenceFrameSample& sample) noexcept {
  if (result_.status != CaptureStatus::collecting ||
      !std::isfinite(sample.frame_time_ms) || sample.frame_time_ms <= 0.0 ||
      (has_frame_ && sample.frame_index <= last_frame_index_)) {
    result_.status = CaptureStatus::failed;
    return result_.status;
  }
  has_frame_ = true;
  last_frame_index_ = sample.frame_index;
  ++seen_frames_;
  if (seen_frames_ <= config_.warmup_frames) {
    return result_.status;
  }

  frame_times_.push_back(sample.frame_time_ms);
  ++result_.accepted_frames;
  result_.dropped_frames += sample.dropped ? 1u : 0u;
  result_.validation_errors += sample.validation_error ? 1u : 0u;
  result_.device_loss_events += sample.device_loss ? 1u : 0u;
  result_.gpu_execution_observed =
      result_.gpu_execution_observed || sample.gpu_execution_observed;
  result_.silent_cpu_fallback_observed =
      result_.silent_cpu_fallback_observed ||
      sample.silent_cpu_fallback_observed;

  if (result_.preview_digest == 0u) {
    result_.preview_digest = sample.preview_digest;
    result_.export_digest = sample.export_digest;
  } else if (result_.preview_digest != sample.preview_digest ||
             result_.export_digest != sample.export_digest) {
    result_.status = CaptureStatus::failed;
  }
  return result_.status;
}

CaptureStatus ProductionEvidenceCapture::finalize(
    std::uint64_t elapsed_seconds) noexcept {
  if (result_.status != CaptureStatus::collecting || frame_times_.empty()) {
    result_.status = CaptureStatus::failed;
    return result_.status;
  }
  result_.soak_seconds = elapsed_seconds;
  double total = 0.0;
  for (const double value : frame_times_) {
    total += value;
  }
  result_.average_frame_ms = total / static_cast<double>(frame_times_.size());
  auto sorted = frame_times_;
  std::sort(sorted.begin(), sorted.end());
  const auto p95_index = static_cast<std::size_t>(
      std::ceil(static_cast<double>(sorted.size()) * 0.95) - 1.0);
  result_.p95_frame_ms = sorted[std::min(p95_index, sorted.size() - 1u)];

  const bool parity_ok = !config_.require_preview_export_parity ||
                         (result_.preview_digest != 0u &&
                          result_.preview_digest == result_.export_digest);
  const bool gpu_ok = !config_.require_gpu_observation ||
                      result_.gpu_execution_observed;
  if (result_.accepted_frames < config_.minimum_frames ||
      elapsed_seconds < config_.minimum_soak_seconds || !parity_ok ||
      !gpu_ok || result_.silent_cpu_fallback_observed) {
    result_.status = CaptureStatus::failed;
    return result_.status;
  }

  result_.status = CaptureStatus::complete;
  std::uint64_t hash = 1469598103934665603ull;
  hash = append(hash, &result_.accepted_frames,
                sizeof(result_.accepted_frames));
  hash = append(hash, &result_.average_frame_ms,
                sizeof(result_.average_frame_ms));
  hash = append(hash, &result_.p95_frame_ms, sizeof(result_.p95_frame_ms));
  hash = append(hash, &result_.preview_digest,
                sizeof(result_.preview_digest));
  result_.digest = hash;
  return result_.status;
}

const EvidenceCaptureResult& ProductionEvidenceCapture::result() const noexcept {
  return result_;
}

const EvidenceCaptureConfig& ProductionEvidenceCapture::config() const noexcept {
  return config_;
}

}  // namespace digitor

extern "C" DigitorEvidenceCaptureHandle digitor_evidence_capture_create(
    const DigitorEvidenceCaptureConfig* config) {
  if (!config || !config->device_id || !config->gpu_name ||
      !config->driver_version || !config->engine_version ||
      !config->source_commit) {
    return nullptr;
  }
  try {
    digitor::EvidenceCaptureConfig native;
    native.platform = static_cast<digitor::CapturePlatform>(config->platform);
    native.backend = static_cast<digitor::CaptureBackend>(config->backend);
    native.device_id = config->device_id;
    native.gpu_name = config->gpu_name;
    native.driver_version = config->driver_version;
    native.engine_version = config->engine_version;
    native.source_commit = config->source_commit;
    native.warmup_frames = config->warmup_frames;
    native.minimum_frames = config->minimum_frames;
    native.minimum_soak_seconds = config->minimum_soak_seconds;
    native.require_preview_export_parity =
        config->require_preview_export_parity != 0u;
    native.require_gpu_observation = config->require_gpu_observation != 0u;
    return new digitor::ProductionEvidenceCapture(std::move(native));
  } catch (...) {
    return nullptr;
  }
}

extern "C" void digitor_evidence_capture_destroy(
    DigitorEvidenceCaptureHandle handle) {
  delete static_cast<digitor::ProductionEvidenceCapture*>(handle);
}

extern "C" std::uint32_t digitor_evidence_capture_add_sample(
    DigitorEvidenceCaptureHandle handle,
    const DigitorEvidenceFrameSample* sample) {
  if (!handle || !sample) {
    return 0u;
  }
  digitor::EvidenceFrameSample native;
  native.frame_index = sample->frame_index;
  native.frame_time_ms = sample->frame_time_ms;
  native.preview_digest = sample->preview_digest;
  native.export_digest = sample->export_digest;
  native.dropped = sample->dropped != 0u;
  native.validation_error = sample->validation_error != 0u;
  native.device_loss = sample->device_loss != 0u;
  native.gpu_execution_observed = sample->gpu_execution_observed != 0u;
  native.silent_cpu_fallback_observed =
      sample->silent_cpu_fallback_observed != 0u;
  return static_cast<std::uint32_t>(
      static_cast<digitor::ProductionEvidenceCapture*>(handle)
          ->add_sample(native));
}

extern "C" std::uint32_t digitor_evidence_capture_finalize(
    DigitorEvidenceCaptureHandle handle, std::uint64_t elapsed_seconds) {
  if (!handle) {
    return 0u;
  }
  return static_cast<std::uint32_t>(
      static_cast<digitor::ProductionEvidenceCapture*>(handle)
          ->finalize(elapsed_seconds));
}

extern "C" std::uint32_t digitor_evidence_capture_result(
    DigitorEvidenceCaptureHandle handle,
    DigitorEvidenceCaptureResult* output) {
  if (!handle || !output) {
    return 1u;
  }
  const auto& result =
      static_cast<digitor::ProductionEvidenceCapture*>(handle)->result();
  output->status = static_cast<std::uint32_t>(result.status);
  output->accepted_frames = result.accepted_frames;
  output->dropped_frames = result.dropped_frames;
  output->validation_errors = result.validation_errors;
  output->device_loss_events = result.device_loss_events;
  output->soak_seconds = result.soak_seconds;
  output->average_frame_ms = result.average_frame_ms;
  output->p95_frame_ms = result.p95_frame_ms;
  output->preview_digest = result.preview_digest;
  output->export_digest = result.export_digest;
  output->gpu_execution_observed = result.gpu_execution_observed ? 1u : 0u;
  output->silent_cpu_fallback_observed =
      result.silent_cpu_fallback_observed ? 1u : 0u;
  output->digest = result.digest;
  return 0u;
}
