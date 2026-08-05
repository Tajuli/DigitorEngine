#include "digitor/production_hardware_evidence.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <set>
#include <string>

namespace digitor {
namespace {

constexpr std::uint32_t kAllPlatformsMask = 0x0Fu;

std::uint64_t append(std::uint64_t hash, const void* data,
                     std::size_t size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ull;
  }
  return hash;
}

std::uint64_t append_string(std::uint64_t hash,
                            const std::string& value) noexcept {
  return append(hash, value.data(), value.size());
}

bool valid_sha256(const std::string& value) noexcept {
  if (value.size() != 64u) {
    return false;
  }
  for (const char character : value) {
    const bool digit = character >= '0' && character <= '9';
    const bool lower = character >= 'a' && character <= 'f';
    const bool upper = character >= 'A' && character <= 'F';
    if (!digit && !lower && !upper) {
      return false;
    }
  }
  return true;
}

bool valid_version(const std::string& value) noexcept {
  if (value.empty()) {
    return false;
  }
  std::uint32_t dots = 0;
  for (const char character : value) {
    if (character == '.') {
      ++dots;
    } else if (character < '0' || character > '9') {
      return false;
    }
  }
  return dots == 2u;
}

bool valid_platform_backend(EvidencePlatform platform,
                            EvidenceBackend backend) noexcept {
  switch (platform) {
    case EvidencePlatform::windows:
      return backend == EvidenceBackend::vulkan ||
             backend == EvidenceBackend::d3d12;
    case EvidencePlatform::android:
      return backend == EvidenceBackend::vulkan ||
             backend == EvidenceBackend::gles;
    case EvidencePlatform::macos:
    case EvidencePlatform::ios:
      return backend == EvidenceBackend::metal;
  }
  return false;
}

std::uint32_t platform_bit(EvidencePlatform platform) noexcept {
  const auto value = static_cast<std::uint32_t>(platform);
  if (value < 1u || value > 4u) {
    return 0u;
  }
  return 1u << (value - 1u);
}

bool valid_policy(const HardwareEvidencePolicy& policy) noexcept {
  return policy.minimum_frames > 0u && policy.minimum_soak_seconds > 0u &&
         std::isfinite(policy.maximum_average_frame_ms) &&
         std::isfinite(policy.maximum_p95_frame_ms) &&
         policy.maximum_average_frame_ms > 0.0 &&
         policy.maximum_p95_frame_ms >= policy.maximum_average_frame_ms;
}

bool record_passes(const HardwareEvidenceRecord& record,
                   const HardwareEvidencePolicy& policy) noexcept {
  if (!valid_platform_backend(record.platform, record.backend) ||
      record.device_id.empty() || record.gpu_name.empty() ||
      record.driver_version.empty() || !valid_version(record.engine_version) ||
      record.source_commit.size() < 7u ||
      !valid_sha256(record.evidence_sha256) || !record.real_device ||
      !record.gpu_execution_observed ||
      record.silent_cpu_fallback_observed ||
      record.rendered_frames < policy.minimum_frames ||
      record.soak_seconds < policy.minimum_soak_seconds ||
      record.dropped_frames > policy.maximum_dropped_frames ||
      record.validation_errors > policy.maximum_validation_errors ||
      record.device_loss_events > policy.maximum_device_loss_events ||
      !std::isfinite(record.average_frame_ms) ||
      !std::isfinite(record.p95_frame_ms) ||
      record.average_frame_ms <= 0.0 || record.p95_frame_ms <= 0.0 ||
      record.average_frame_ms > policy.maximum_average_frame_ms ||
      record.p95_frame_ms > policy.maximum_p95_frame_ms) {
    return false;
  }
  if (policy.require_signed_attestation && !record.signed_attestation) {
    return false;
  }
  if (policy.require_preview_export_parity &&
      (record.preview_digest == 0u ||
       record.preview_digest != record.export_digest)) {
    return false;
  }
  return true;
}

std::uint64_t digest_record(std::uint64_t hash,
                            const HardwareEvidenceRecord& record) noexcept {
  hash = append(hash, &record.platform, sizeof(record.platform));
  hash = append(hash, &record.backend, sizeof(record.backend));
  hash = append_string(hash, record.device_id);
  hash = append_string(hash, record.gpu_name);
  hash = append_string(hash, record.driver_version);
  hash = append_string(hash, record.engine_version);
  hash = append_string(hash, record.source_commit);
  hash = append_string(hash, record.evidence_sha256);
  hash = append(hash, &record.rendered_frames, sizeof(record.rendered_frames));
  hash = append(hash, &record.soak_seconds, sizeof(record.soak_seconds));
  hash = append(hash, &record.average_frame_ms,
                sizeof(record.average_frame_ms));
  hash = append(hash, &record.p95_frame_ms, sizeof(record.p95_frame_ms));
  hash = append(hash, &record.preview_digest, sizeof(record.preview_digest));
  hash = append(hash, &record.export_digest, sizeof(record.export_digest));
  return hash;
}

}  // namespace

HardwareEvidenceResult validate_hardware_evidence(
    const std::vector<HardwareEvidenceRecord>& records,
    const HardwareEvidencePolicy& policy) noexcept {
  HardwareEvidenceResult result;
  if (records.empty() || !valid_policy(policy)) {
    return result;
  }

  const std::string expected_version = records.front().engine_version;
  const std::string expected_commit = records.front().source_commit;
  std::set<std::string> unique_devices;
  std::set<std::string> unique_hashes;
  std::uint64_t hash = 1469598103934665603ull;

  for (std::size_t index = 0; index < records.size(); ++index) {
    const auto& record = records[index];
    if (record.engine_version != expected_version ||
        record.source_commit != expected_commit ||
        !unique_devices.insert(record.device_id).second ||
        !unique_hashes.insert(record.evidence_sha256).second ||
        !record_passes(record, policy)) {
      result.failed_record_index = static_cast<std::uint32_t>(index);
      return result;
    }
    const auto bit = platform_bit(record.platform);
    if (bit == 0u || (result.qualified_platform_mask & bit) != 0u) {
      result.failed_record_index = static_cast<std::uint32_t>(index);
      return result;
    }
    result.qualified_platform_mask |= bit;
    hash = digest_record(hash, record);
  }

  result.missing_platform_mask =
      kAllPlatformsMask & ~result.qualified_platform_mask;
  result.status = result.missing_platform_mask == 0u
                      ? EvidenceStatus::qualified
                      : EvidenceStatus::blocked;
  result.digest = hash;
  return result;
}

}  // namespace digitor

extern "C" std::uint32_t digitor_validate_hardware_evidence(
    const DigitorHardwareEvidenceRecord* records,
    std::uint32_t record_count,
    const DigitorHardwareEvidencePolicy* policy,
    DigitorHardwareEvidenceResult* output) {
  if (!records || record_count == 0u || !policy || !output) {
    return 1u;
  }
  try {
    digitor::HardwareEvidencePolicy native_policy;
    native_policy.minimum_frames = policy->minimum_frames;
    native_policy.minimum_soak_seconds = policy->minimum_soak_seconds;
    native_policy.maximum_dropped_frames = policy->maximum_dropped_frames;
    native_policy.maximum_validation_errors = policy->maximum_validation_errors;
    native_policy.maximum_device_loss_events = policy->maximum_device_loss_events;
    native_policy.maximum_average_frame_ms = policy->maximum_average_frame_ms;
    native_policy.maximum_p95_frame_ms = policy->maximum_p95_frame_ms;
    native_policy.require_signed_attestation =
        policy->require_signed_attestation != 0u;
    native_policy.require_preview_export_parity =
        policy->require_preview_export_parity != 0u;

    std::vector<digitor::HardwareEvidenceRecord> native_records;
    native_records.reserve(record_count);
    for (std::uint32_t index = 0; index < record_count; ++index) {
      const auto& source = records[index];
      if (!source.device_id || !source.gpu_name || !source.driver_version ||
          !source.engine_version || !source.source_commit ||
          !source.evidence_sha256) {
        return 2u;
      }
      digitor::HardwareEvidenceRecord record;
      record.platform = static_cast<digitor::EvidencePlatform>(source.platform);
      record.backend = static_cast<digitor::EvidenceBackend>(source.backend);
      record.device_id = source.device_id;
      record.gpu_name = source.gpu_name;
      record.driver_version = source.driver_version;
      record.engine_version = source.engine_version;
      record.source_commit = source.source_commit;
      record.evidence_sha256 = source.evidence_sha256;
      record.rendered_frames = source.rendered_frames;
      record.dropped_frames = source.dropped_frames;
      record.validation_errors = source.validation_errors;
      record.device_loss_events = source.device_loss_events;
      record.soak_seconds = source.soak_seconds;
      record.average_frame_ms = source.average_frame_ms;
      record.p95_frame_ms = source.p95_frame_ms;
      record.preview_digest = source.preview_digest;
      record.export_digest = source.export_digest;
      record.real_device = source.real_device != 0u;
      record.signed_attestation = source.signed_attestation != 0u;
      record.gpu_execution_observed = source.gpu_execution_observed != 0u;
      record.silent_cpu_fallback_observed =
          source.silent_cpu_fallback_observed != 0u;
      native_records.push_back(std::move(record));
    }

    const auto result =
        digitor::validate_hardware_evidence(native_records, native_policy);
    output->status = static_cast<std::uint32_t>(result.status);
    output->qualified_platform_mask = result.qualified_platform_mask;
    output->missing_platform_mask = result.missing_platform_mask;
    output->failed_record_index = result.failed_record_index;
    output->digest = result.digest;
    return 0u;
  } catch (...) {
    return 3u;
  }
}
