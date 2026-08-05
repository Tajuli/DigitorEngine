#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class RecoveryStatus : std::uint32_t {
  ready = 0,
  invalid = 1,
  device_lost = 2,
  recovering = 3,
  recovered = 4,
  exhausted = 5,
  failed = 6,
};

enum class BackendKind : std::uint32_t {
  cpu = 0,
  vulkan = 1,
  d3d12 = 2,
  metal = 3,
  gles = 4,
};

struct RecoveryPolicy {
  std::uint32_t max_attempts{3};
  std::uint32_t backoff_ms{50};
  bool invalidate_pipeline_cache{true};
  bool invalidate_frame_resources{true};
};

struct RecoverySnapshot {
  RecoveryStatus status{RecoveryStatus::ready};
  BackendKind backend{BackendKind::cpu};
  std::uint32_t attempts{0};
  std::uint64_t device_generation{1};
  std::uint64_t cache_generation{1};
  std::uint64_t invalidated_frames{0};
  std::uint64_t digest{0};
};

struct ReleaseManifest {
  std::string version;
  std::uint32_t abi_major{0};
  std::uint32_t abi_minor{0};
  std::string commit;
  std::vector<BackendKind> compiled_backends;
  bool preview_export_parity_required{true};
  bool silent_cpu_fallback_forbidden{true};
};

using RecreateBackend = bool (*)(BackendKind backend,
                                 std::uint64_t next_device_generation,
                                 void* user_data);

class ProductionRecoveryController {
 public:
  ProductionRecoveryController(BackendKind backend, RecoveryPolicy policy);

  RecoveryStatus notify_device_lost(std::uint64_t in_flight_frames) noexcept;
  RecoveryStatus attempt_recovery(RecreateBackend callback,
                                  void* user_data) noexcept;
  const RecoverySnapshot& snapshot() const noexcept;

 private:
  BackendKind backend_;
  RecoveryPolicy policy_;
  RecoverySnapshot snapshot_;
};

bool validate_release_manifest(const ReleaseManifest& manifest) noexcept;
std::uint64_t release_manifest_digest(const ReleaseManifest& manifest) noexcept;

}  // namespace digitor

extern "C" {

struct DigitorRecoveryPolicy {
  std::uint32_t max_attempts;
  std::uint32_t backoff_ms;
  std::uint32_t invalidate_pipeline_cache;
  std::uint32_t invalidate_frame_resources;
};

struct DigitorRecoverySnapshot {
  std::uint32_t status;
  std::uint32_t backend;
  std::uint32_t attempts;
  std::uint64_t device_generation;
  std::uint64_t cache_generation;
  std::uint64_t invalidated_frames;
  std::uint64_t digest;
};

using DigitorRecoveryHandle = void*;
using DigitorRecreateBackendFn = std::uint32_t (*)(
    std::uint32_t backend, std::uint64_t generation, void* user_data);

DigitorRecoveryHandle digitor_recovery_create(
    std::uint32_t backend, const DigitorRecoveryPolicy* policy);
void digitor_recovery_destroy(DigitorRecoveryHandle handle);
std::uint32_t digitor_recovery_notify_device_lost(
    DigitorRecoveryHandle handle, std::uint64_t in_flight_frames);
std::uint32_t digitor_recovery_attempt(DigitorRecoveryHandle handle,
                                       DigitorRecreateBackendFn callback,
                                       void* user_data);
std::uint32_t digitor_recovery_snapshot(DigitorRecoveryHandle handle,
                                        DigitorRecoverySnapshot* output);

}