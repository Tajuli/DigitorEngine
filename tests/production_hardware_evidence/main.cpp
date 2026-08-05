#include "digitor/production_hardware_evidence.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {

digitor::HardwareEvidenceRecord make_record(
    digitor::EvidencePlatform platform, digitor::EvidenceBackend backend,
    const char* device, char hash_character) {
  digitor::HardwareEvidenceRecord record;
  record.platform = platform;
  record.backend = backend;
  record.device_id = device;
  record.gpu_name = std::string("GPU-") + device;
  record.driver_version = "1.2.3";
  record.engine_version = "4.9.0";
  record.source_commit = "145fa4586a31cdc518d8405b6de3c94c5d68f274";
  record.evidence_sha256 = std::string(64u, hash_character);
  record.rendered_frames = 20000u;
  record.soak_seconds = 900u;
  record.average_frame_ms = 12.0;
  record.p95_frame_ms = 19.0;
  record.preview_digest = 0xABCDEFu;
  record.export_digest = 0xABCDEFu;
  record.real_device = true;
  record.signed_attestation = true;
  record.gpu_execution_observed = true;
  return record;
}

}  // namespace

int main() {
  using namespace digitor;

  HardwareEvidencePolicy policy;
  std::vector<HardwareEvidenceRecord> records;
  records.push_back(make_record(EvidencePlatform::windows,
                                EvidenceBackend::d3d12, "windows-device", 'a'));
  records.push_back(make_record(EvidencePlatform::android,
                                EvidenceBackend::vulkan, "android-device", 'b'));
  records.push_back(make_record(EvidencePlatform::macos,
                                EvidenceBackend::metal, "macos-device", 'c'));
  records.push_back(make_record(EvidencePlatform::ios,
                                EvidenceBackend::metal, "ios-device", 'd'));

  const auto qualified = validate_hardware_evidence(records, policy);
  if (qualified.status != EvidenceStatus::qualified ||
      qualified.qualified_platform_mask != 0x0Fu ||
      qualified.missing_platform_mask != 0u || qualified.digest == 0u) {
    return 1;
  }

  auto partial = records;
  partial.pop_back();
  const auto blocked = validate_hardware_evidence(partial, policy);
  if (blocked.status != EvidenceStatus::blocked ||
      blocked.missing_platform_mask != 0x08u) {
    return 2;
  }

  auto fallback = records;
  fallback[1].silent_cpu_fallback_observed = true;
  const auto invalid_fallback = validate_hardware_evidence(fallback, policy);
  if (invalid_fallback.status != EvidenceStatus::invalid ||
      invalid_fallback.failed_record_index != 1u) {
    return 3;
  }

  auto parity = records;
  parity[2].export_digest += 1u;
  if (validate_hardware_evidence(parity, policy).status !=
      EvidenceStatus::invalid) {
    return 4;
  }

  auto duplicate = records;
  duplicate[3].evidence_sha256 = duplicate[2].evidence_sha256;
  if (validate_hardware_evidence(duplicate, policy).status !=
      EvidenceStatus::invalid) {
    return 5;
  }

  DigitorHardwareEvidenceRecord c_records[4]{};
  for (std::uint32_t index = 0; index < 4u; ++index) {
    const auto& source = records[index];
    auto& target = c_records[index];
    target.platform = static_cast<std::uint32_t>(source.platform);
    target.backend = static_cast<std::uint32_t>(source.backend);
    target.device_id = source.device_id.c_str();
    target.gpu_name = source.gpu_name.c_str();
    target.driver_version = source.driver_version.c_str();
    target.engine_version = source.engine_version.c_str();
    target.source_commit = source.source_commit.c_str();
    target.evidence_sha256 = source.evidence_sha256.c_str();
    target.rendered_frames = source.rendered_frames;
    target.soak_seconds = source.soak_seconds;
    target.average_frame_ms = source.average_frame_ms;
    target.p95_frame_ms = source.p95_frame_ms;
    target.preview_digest = source.preview_digest;
    target.export_digest = source.export_digest;
    target.real_device = 1u;
    target.signed_attestation = 1u;
    target.gpu_execution_observed = 1u;
  }

  DigitorHardwareEvidencePolicy c_policy{};
  c_policy.minimum_frames = policy.minimum_frames;
  c_policy.minimum_soak_seconds = policy.minimum_soak_seconds;
  c_policy.maximum_average_frame_ms = policy.maximum_average_frame_ms;
  c_policy.maximum_p95_frame_ms = policy.maximum_p95_frame_ms;
  c_policy.require_signed_attestation = 1u;
  c_policy.require_preview_export_parity = 1u;
  DigitorHardwareEvidenceResult c_result{};
  if (digitor_validate_hardware_evidence(c_records, 4u, &c_policy,
                                         &c_result) != 0u ||
      c_result.status != static_cast<std::uint32_t>(EvidenceStatus::qualified) ||
      c_result.digest == 0u) {
    return 6;
  }

  return 0;
}
