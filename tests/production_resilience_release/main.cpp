#include "digitor/production_resilience_release.hpp"

#include <cstdint>

namespace {

bool recreate_success(digitor::BackendKind, std::uint64_t generation,
                      void* user_data) {
  auto* observed = static_cast<std::uint64_t*>(user_data);
  *observed = generation;
  return true;
}

bool recreate_failure(digitor::BackendKind, std::uint64_t, void*) {
  return false;
}

std::uint32_t recreate_c(std::uint32_t, std::uint64_t generation,
                         void* user_data) {
  auto* observed = static_cast<std::uint64_t*>(user_data);
  *observed = generation;
  return 1u;
}

}  // namespace

int main() {
  using namespace digitor;

  ReleaseManifest manifest{"5.0.0", 5u, 0u, "086a5ede46b4546",
                           {BackendKind::vulkan, BackendKind::d3d12,
                            BackendKind::metal, BackendKind::gles,
                            BackendKind::cpu},
                           true, true};
  if (!validate_release_manifest(manifest) ||
      release_manifest_digest(manifest) == 0u) {
    return 1;
  }

  RecoveryPolicy policy;
  policy.max_attempts = 3u;
  ProductionRecoveryController controller(BackendKind::vulkan, policy);
  if (controller.notify_device_lost(4u) != RecoveryStatus::device_lost) {
    return 2;
  }
  if (controller.snapshot().invalidated_frames != 4u ||
      controller.snapshot().cache_generation != 2u) {
    return 3;
  }

  std::uint64_t observed_generation = 0u;
  if (controller.attempt_recovery(recreate_success, &observed_generation) !=
          RecoveryStatus::recovered ||
      observed_generation != 2u || controller.snapshot().digest == 0u) {
    return 4;
  }

  RecoveryPolicy single_attempt;
  single_attempt.max_attempts = 1u;
  ProductionRecoveryController exhausted(BackendKind::metal, single_attempt);
  exhausted.notify_device_lost(1u);
  if (exhausted.attempt_recovery(recreate_failure, nullptr) !=
      RecoveryStatus::exhausted) {
    return 5;
  }

  DigitorRecoveryPolicy c_policy{2u, 10u, 1u, 1u};
  const auto handle = digitor_recovery_create(
      static_cast<std::uint32_t>(BackendKind::d3d12), &c_policy);
  if (!handle) {
    return 6;
  }
  if (digitor_recovery_notify_device_lost(handle, 7u) !=
      static_cast<std::uint32_t>(RecoveryStatus::device_lost)) {
    digitor_recovery_destroy(handle);
    return 7;
  }
  observed_generation = 0u;
  if (digitor_recovery_attempt(handle, recreate_c, &observed_generation) !=
          static_cast<std::uint32_t>(RecoveryStatus::recovered) ||
      observed_generation != 2u) {
    digitor_recovery_destroy(handle);
    return 8;
  }
  DigitorRecoverySnapshot snapshot{};
  if (digitor_recovery_snapshot(handle, &snapshot) != 0u ||
      snapshot.invalidated_frames != 7u || snapshot.device_generation != 2u ||
      snapshot.digest == 0u) {
    digitor_recovery_destroy(handle);
    return 9;
  }
  digitor_recovery_destroy(handle);

  manifest.preview_export_parity_required = false;
  if (validate_release_manifest(manifest)) {
    return 10;
  }

  ProductionRecoveryController invalid(BackendKind::cpu, policy);
  if (invalid.snapshot().status != RecoveryStatus::invalid) {
    return 11;
  }

  return 0;
}
