#pragma once

#include <cstdint>

namespace digitor {

enum class AndroidGpuBackend : std::uint32_t { unknown = 0, vulkan = 1, gles = 2 };
enum class AndroidGpuQualificationStatus : std::uint32_t {
  invalid = 0,
  qualified = 1,
  blocked_software_renderer = 2,
  blocked_emulator = 3,
  blocked_fallback = 4,
  blocked_execution = 5,
  blocked_parity = 6
};

struct AndroidGpuQualificationInput final {
  AndroidGpuBackend backend{AndroidGpuBackend::unknown};
  const char* manufacturer{};
  const char* model{};
  const char* hardware{};
  const char* renderer{};
  const char* driver_version{};
  std::uint32_t sdk_level{};
  std::uint32_t is_physical_device{};
  std::uint32_t native_submission_completed{};
  std::uint32_t gpu_timestamp_valid{};
  std::uint32_t cpu_readbacks{};
  std::uint32_t cpu_reuploads{};
  std::uint32_t fallback_dispatches{};
  std::uint64_t preview_digest{};
  std::uint64_t export_digest{};
};

struct AndroidGpuQualificationResult final {
  AndroidGpuQualificationStatus status{AndroidGpuQualificationStatus::invalid};
  std::uint64_t evidence_digest{};
};

AndroidGpuQualificationResult qualify_android_gpu(
    const AndroidGpuQualificationInput& input) noexcept;

}  // namespace digitor

extern "C" {
struct DigitorAndroidGpuQualificationInput {
  std::uint32_t backend;
  const char* manufacturer;
  const char* model;
  const char* hardware;
  const char* renderer;
  const char* driver_version;
  std::uint32_t sdk_level;
  std::uint32_t is_physical_device;
  std::uint32_t native_submission_completed;
  std::uint32_t gpu_timestamp_valid;
  std::uint32_t cpu_readbacks;
  std::uint32_t cpu_reuploads;
  std::uint32_t fallback_dispatches;
  std::uint64_t preview_digest;
  std::uint64_t export_digest;
};
struct DigitorAndroidGpuQualificationResult {
  std::uint32_t status;
  std::uint64_t evidence_digest;
};
std::uint32_t digitor_qualify_android_gpu(
    const DigitorAndroidGpuQualificationInput* input,
    DigitorAndroidGpuQualificationResult* output);
}
