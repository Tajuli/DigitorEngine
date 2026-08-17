#pragma once

#include "digitor/digitor.h"
#include "digitor/gpu_frame.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

enum class AndroidZeroCopyBackend : std::uint32_t {
  none = 0,
  vulkan = 1,
  opengl_es = 2,
};

enum class AndroidYuvFormat : std::uint32_t {
  yuv420_888 = 1,
  nv12 = 2,
  p010 = 3,
  implementation_defined = 4,
};

enum class AndroidColorMatrix : std::uint32_t {
  bt601 = 1,
  bt709 = 2,
  bt2020_ncl = 3,
};

enum class AndroidColorRange : std::uint32_t {
  limited = 1,
  full = 2,
};

struct AndroidHardwareBufferFrame {
  void* hardware_buffer{};          // AHardwareBuffer*
  int acquire_fence_fd{-1};         // owned by importer after successful submit
  std::uint32_t width{};
  std::uint32_t height{};
  AndroidYuvFormat format{AndroidYuvFormat::implementation_defined};
  AndroidColorMatrix matrix{AndroidColorMatrix::bt709};
  AndroidColorRange range{AndroidColorRange::limited};
  std::uint32_t bit_depth{8};
  std::int64_t timestamp_us{};
  std::uint64_t frame_identity{};
  std::shared_ptr<void> decoder_lifetime;
};

struct AndroidZeroCopyConfig {
  AndroidZeroCopyBackend preferred_backend{AndroidZeroCopyBackend::vulkan};
  bool allow_gles_fallback{true};
  bool strict_gpu_first{true};
  bool require_external_memory{true};
  bool require_external_fence{true};
  bool require_rgba16f_output{true};
  bool require_preview_export_identity{true};
  std::uint32_t max_in_flight_frames{6};
  std::uint32_t frame_timeout_ms{250};
  std::string device_fingerprint;
  std::string gpu_name;
  std::string driver_version;
  std::string engine_commit;
};

struct AndroidImportedImage {
  AndroidZeroCopyBackend backend{AndroidZeroCopyBackend::none};
  void* image{};                    // VkImage or EGLImageKHR
  void* image_view{};               // VkImageView or GL texture handle wrapper
  void* completion_sync{};          // VkSemaphore or EGLSyncKHR wrapper
  std::uint64_t completion_value{};
  std::uint32_t width{};
  std::uint32_t height{};
  AndroidYuvFormat format{AndroidYuvFormat::implementation_defined};
  AndroidColorMatrix matrix{AndroidColorMatrix::bt709};
  AndroidColorRange range{AndroidColorRange::limited};
  std::int32_t primaries{};
  std::int32_t transfer{};
  std::int64_t timestamp_us{};
  std::uint64_t frame_identity{};
  std::shared_ptr<void> lifetime;
};

using AndroidMediaCodecAcquire = std::function<DigitorResult(
    std::int64_t timestamp_us, AndroidHardwareBufferFrame&)>;
using AndroidVulkanImport = std::function<DigitorResult(
    const AndroidHardwareBufferFrame&, AndroidImportedImage&)>;
using AndroidGlesImport = std::function<DigitorResult(
    const AndroidHardwareBufferFrame&, AndroidImportedImage&)>;
using AndroidYuvToRgba16f = std::function<DigitorResult(
    const AndroidImportedImage&, ProcessedGpuFramePtr&)>;
using AndroidGpuFrameConsumer = std::function<DigitorResult(
    const ProcessedGpuFramePtr&)>;

struct AndroidZeroCopyBinding {
  AndroidMediaCodecAcquire acquire_decoder_frame;
  AndroidVulkanImport import_vulkan;
  AndroidGlesImport import_gles;
  AndroidYuvToRgba16f convert_to_rgba16f;
  AndroidGpuFrameConsumer preview_consumer;
  AndroidGpuFrameConsumer encoder_consumer;
};

struct AndroidZeroCopyTelemetry {
  AndroidZeroCopyBackend active_backend{AndroidZeroCopyBackend::none};
  std::uint64_t frames_requested{};
  std::uint64_t decoder_surfaces{};
  std::uint64_t imported_images{};
  std::uint64_t rgba16f_frames{};
  std::uint64_t preview_frames{};
  std::uint64_t encoder_frames{};
  std::uint64_t import_failures{};
  std::uint64_t synchronization_failures{};
  std::uint64_t identity_mismatches{};
  std::uint64_t timestamp_mismatches{};
  std::uint64_t cpu_copies{};
  std::uint64_t cpu_fallback_frames{};
  bool quarantined{};
  std::string diagnostic;
};

class AndroidZeroCopyPipeline final {
public:
  AndroidZeroCopyPipeline(AndroidZeroCopyConfig, AndroidZeroCopyBinding);
  ~AndroidZeroCopyPipeline();

  AndroidZeroCopyPipeline(const AndroidZeroCopyPipeline&) = delete;
  AndroidZeroCopyPipeline& operator=(const AndroidZeroCopyPipeline&) = delete;

  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult preview(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult export_frame(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult preview_and_export(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult quarantine(const char* reason) noexcept;
  [[nodiscard]] DigitorResult reset_quarantine() noexcept;
  [[nodiscard]] AndroidZeroCopyTelemetry telemetry() const;
  [[nodiscard]] bool production_active() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace digitor
