#include "digitor/production_resilience_release.hpp"

#include <algorithm>
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

bool valid_backend(BackendKind backend) noexcept {
  const auto value = static_cast<std::uint32_t>(backend);
  return value <= static_cast<std::uint32_t>(BackendKind::gles);
}

bool valid_version(const std::string& version) noexcept {
  if (version.empty()) {
    return false;
  }
  return std::count(version.begin(), version.end(), '.') == 2;
}

}  // namespace

ProductionRecoveryController::ProductionRecoveryController(
    BackendKind backend, RecoveryPolicy policy)
    : backend_(backend), policy_(policy) {
  snapshot_.backend = backend;
  if (!valid_backend(backend_) || backend_ == BackendKind::cpu ||
      policy_.max_attempts == 0u || policy_.max_attempts > 16u ||
      policy_.backoff_ms > 60000u) {
    snapshot_.status = RecoveryStatus::invalid;
  }
}

RecoveryStatus ProductionRecoveryController::notify_device_lost(
    std::uint64_t in_flight_frames) noexcept {
  if (snapshot_.status == RecoveryStatus::invalid ||
      snapshot_.status == RecoveryStatus::exhausted) {
    return snapshot_.status;
  }
  snapshot_.status = RecoveryStatus::device_lost;
  if (policy_.invalidate_frame_resources) {
    snapshot_.invalidated_frames += in_flight_frames;
  }
  if (policy_.invalidate_pipeline_cache) {
    ++snapshot_.cache_generation;
  }
  snapshot_.digest = release_manifest_digest(
      ReleaseManifest{"recovery", 1u, 0u, "runtime", {backend_}, true, true});
  return snapshot_.status;
}

RecoveryStatus ProductionRecoveryController::attempt_recovery(
    RecreateBackend callback, void* user_data) noexcept {
  if (snapshot_.status != RecoveryStatus::device_lost &&
      snapshot_.status != RecoveryStatus::recovering) {
    return snapshot_.status;
  }
  if (!callback) {
    snapshot_.status = RecoveryStatus::failed;
    return snapshot_.status;
  }
  if (snapshot_.attempts >= policy_.max_attempts) {
    snapshot_.status = RecoveryStatus::exhausted;
    return snapshot_.status;
  }

  snapshot_.status = RecoveryStatus::recovering;
  ++snapshot_.attempts;
  const auto next_generation = snapshot_.device_generation + 1u;
  if (!callback(backend_, next_generation, user_data)) {
    snapshot_.status = snapshot_.attempts >= policy_.max_attempts
                           ? RecoveryStatus::exhausted
                           : RecoveryStatus::device_lost;
    return snapshot_.status;
  }

  snapshot_.device_generation = next_generation;
  snapshot_.status = RecoveryStatus::recovered;
  snapshot_.digest = append(1469598103934665603ull, &snapshot_,
                            sizeof(snapshot_) - sizeof(snapshot_.digest));
  return snapshot_.status;
}

const RecoverySnapshot& ProductionRecoveryController::snapshot() const noexcept {
  return snapshot_;
}

bool validate_release_manifest(const ReleaseManifest& manifest) noexcept {
  if (!valid_version(manifest.version) || manifest.abi_major == 0u ||
      manifest.commit.size() < 7u || manifest.compiled_backends.empty() ||
      !manifest.preview_export_parity_required ||
      !manifest.silent_cpu_fallback_forbidden) {
    return false;
  }
  bool gpu_found = false;
  for (const auto backend : manifest.compiled_backends) {
    if (!valid_backend(backend)) {
      return false;
    }
    gpu_found = gpu_found || backend != BackendKind::cpu;
  }
  return gpu_found;
}

std::uint64_t release_manifest_digest(const ReleaseManifest& manifest) noexcept {
  std::uint64_t hash = 1469598103934665603ull;
  hash = append(hash, manifest.version.data(), manifest.version.size());
  hash = append(hash, &manifest.abi_major, sizeof(manifest.abi_major));
  hash = append(hash, &manifest.abi_minor, sizeof(manifest.abi_minor));
  hash = append(hash, manifest.commit.data(), manifest.commit.size());
  for (const auto backend : manifest.compiled_backends) {
    hash = append(hash, &backend, sizeof(backend));
  }
  hash = append(hash, &manifest.preview_export_parity_required,
                sizeof(manifest.preview_export_parity_required));
  hash = append(hash, &manifest.silent_cpu_fallback_forbidden,
                sizeof(manifest.silent_cpu_fallback_forbidden));
  return hash;
}

}  // namespace digitor

extern "C" DigitorRecoveryHandle digitor_recovery_create(
    std::uint32_t backend, const DigitorRecoveryPolicy* policy) {
  if (!policy) {
    return nullptr;
  }
  try {
    digitor::RecoveryPolicy native;
    native.max_attempts = policy->max_attempts;
    native.backoff_ms = policy->backoff_ms;
    native.invalidate_pipeline_cache =
        policy->invalidate_pipeline_cache != 0u;
    native.invalidate_frame_resources =
        policy->invalidate_frame_resources != 0u;
    return new digitor::ProductionRecoveryController(
        static_cast<digitor::BackendKind>(backend), native);
  } catch (...) {
    return nullptr;
  }
}

extern "C" void digitor_recovery_destroy(DigitorRecoveryHandle handle) {
  delete static_cast<digitor::ProductionRecoveryController*>(handle);
}

extern "C" std::uint32_t digitor_recovery_notify_device_lost(
    DigitorRecoveryHandle handle, std::uint64_t in_flight_frames) {
  if (!handle) {
    return static_cast<std::uint32_t>(digitor::RecoveryStatus::invalid);
  }
  return static_cast<std::uint32_t>(
      static_cast<digitor::ProductionRecoveryController*>(handle)
          ->notify_device_lost(in_flight_frames));
}

extern "C" std::uint32_t digitor_recovery_attempt(
    DigitorRecoveryHandle handle, DigitorRecreateBackendFn callback,
    void* user_data) {
  if (!handle || !callback) {
    return static_cast<std::uint32_t>(digitor::RecoveryStatus::invalid);
  }
  const auto adapter = +[](digitor::BackendKind backend,
                           std::uint64_t generation, void* opaque) -> bool {
    auto* values = static_cast<void**>(opaque);
    const auto fn = reinterpret_cast<DigitorRecreateBackendFn>(values[0]);
    return fn(static_cast<std::uint32_t>(backend), generation, values[1]) != 0u;
  };
  void* values[2] = {reinterpret_cast<void*>(callback), user_data};
  return static_cast<std::uint32_t>(
      static_cast<digitor::ProductionRecoveryController*>(handle)
          ->attempt_recovery(adapter, values));
}

extern "C" std::uint32_t digitor_recovery_snapshot(
    DigitorRecoveryHandle handle, DigitorRecoverySnapshot* output) {
  if (!handle || !output) {
    return 1u;
  }
  const auto& snapshot =
      static_cast<digitor::ProductionRecoveryController*>(handle)->snapshot();
  output->status = static_cast<std::uint32_t>(snapshot.status);
  output->backend = static_cast<std::uint32_t>(snapshot.backend);
  output->attempts = snapshot.attempts;
  output->device_generation = snapshot.device_generation;
  output->cache_generation = snapshot.cache_generation;
  output->invalidated_frames = snapshot.invalidated_frames;
  output->digest = snapshot.digest;
  return 0u;
}
