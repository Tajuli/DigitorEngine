#include "digitor/android_gpu_qualification.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace digitor {
namespace {

std::uint64_t append(std::uint64_t hash, const void* data, std::size_t size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

std::uint64_t append_text(std::uint64_t hash, const char* text) noexcept {
  return text == nullptr ? hash : append(hash, text, std::strlen(text));
}

bool nonempty(const char* value) noexcept { return value != nullptr && value[0] != '\0'; }

std::string lower(const char* value) {
  std::string output = value == nullptr ? std::string{} : std::string{value};
  std::transform(output.begin(), output.end(), output.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return output;
}

bool contains_any(const std::string& value) {
  static constexpr const char* blocked[] = {
      "swiftshader", "llvmpipe", "softpipe", "software rasterizer",
      "emulator", "goldfish", "ranchu", "angle (swiftshader)"};
  for (const char* token : blocked) {
    if (value.find(token) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

AndroidGpuQualificationResult qualify_android_gpu(
    const AndroidGpuQualificationInput& input) noexcept {
  AndroidGpuQualificationResult result;
  if ((input.backend != AndroidGpuBackend::vulkan &&
       input.backend != AndroidGpuBackend::gles) ||
      !nonempty(input.manufacturer) || !nonempty(input.model) ||
      !nonempty(input.hardware) || !nonempty(input.renderer) ||
      !nonempty(input.driver_version) || input.sdk_level < 23u ||
      input.preview_digest == 0u || input.export_digest == 0u) {
    return result;
  }

  if (input.is_physical_device == 0u || contains_any(lower(input.hardware)) ||
      contains_any(lower(input.model))) {
    result.status = AndroidGpuQualificationStatus::blocked_emulator;
    return result;
  }
  if (contains_any(lower(input.renderer))) {
    result.status = AndroidGpuQualificationStatus::blocked_software_renderer;
    return result;
  }
  if (input.cpu_readbacks != 0u || input.cpu_reuploads != 0u ||
      input.fallback_dispatches != 0u) {
    result.status = AndroidGpuQualificationStatus::blocked_fallback;
    return result;
  }
  if (input.native_submission_completed == 0u || input.gpu_timestamp_valid == 0u) {
    result.status = AndroidGpuQualificationStatus::blocked_execution;
    return result;
  }
  if (input.preview_digest != input.export_digest) {
    result.status = AndroidGpuQualificationStatus::blocked_parity;
    return result;
  }

  std::uint64_t hash = 1469598103934665603ull;
  hash = append(hash, &input.backend, sizeof(input.backend));
  hash = append_text(hash, input.manufacturer);
  hash = append_text(hash, input.model);
  hash = append_text(hash, input.hardware);
  hash = append_text(hash, input.renderer);
  hash = append_text(hash, input.driver_version);
  hash = append(hash, &input.sdk_level, sizeof(input.sdk_level));
  hash = append(hash, &input.preview_digest, sizeof(input.preview_digest));
  result.status = AndroidGpuQualificationStatus::qualified;
  result.evidence_digest = hash;
  return result;
}

}  // namespace digitor

extern "C" std::uint32_t digitor_qualify_android_gpu(
    const DigitorAndroidGpuQualificationInput* input,
    DigitorAndroidGpuQualificationResult* output) {
  if (input == nullptr || output == nullptr) return 1u;
  digitor::AndroidGpuQualificationInput native;
  native.backend = static_cast<digitor::AndroidGpuBackend>(input->backend);
  native.manufacturer = input->manufacturer;
  native.model = input->model;
  native.hardware = input->hardware;
  native.renderer = input->renderer;
  native.driver_version = input->driver_version;
  native.sdk_level = input->sdk_level;
  native.is_physical_device = input->is_physical_device;
  native.native_submission_completed = input->native_submission_completed;
  native.gpu_timestamp_valid = input->gpu_timestamp_valid;
  native.cpu_readbacks = input->cpu_readbacks;
  native.cpu_reuploads = input->cpu_reuploads;
  native.fallback_dispatches = input->fallback_dispatches;
  native.preview_digest = input->preview_digest;
  native.export_digest = input->export_digest;
  const auto result = digitor::qualify_android_gpu(native);
  output->status = static_cast<std::uint32_t>(result.status);
  output->evidence_digest = result.evidence_digest;
  return 0u;
}
