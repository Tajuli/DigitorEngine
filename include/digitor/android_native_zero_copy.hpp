#pragma once

#include "digitor/android_zero_copy_pipeline.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

struct AndroidVulkanNativeContext {
  void* instance{};
  void* physical_device{};
  void* device{};
  void* queue{};
  std::uint32_t queue_family_index{};
};

struct AndroidEglNativeContext {
  void* display{};
  void* context{};
};

struct AndroidNativeInteropConfig {
  AndroidVulkanNativeContext vulkan;
  AndroidEglNativeContext egl;
  bool require_vulkan_external_memory{true};
  bool require_sync_fd{true};
  bool allow_gles_external_image{true};
  bool require_rgba16f{true};
};

struct AndroidNativeInteropTelemetry {
  std::uint64_t vulkan_imports{};
  std::uint64_t egl_imports{};
  std::uint64_t sync_fd_imports{};
  std::uint64_t rgba16f_dispatches{};
  std::uint64_t encoder_submissions{};
  std::uint64_t cpu_copies{};
  std::uint64_t failures{};
  std::string diagnostic;
};

using AndroidVulkanRgba16fDispatch = std::function<DigitorResult(
    const AndroidImportedImage&, ProcessedGpuFramePtr&)>;
using AndroidGlesRgba16fDispatch = std::function<DigitorResult(
    const AndroidImportedImage&, ProcessedGpuFramePtr&)>;
using AndroidMediaCodecSurfaceSubmit = std::function<DigitorResult(
    const ProcessedGpuFramePtr&)>;

class AndroidNativeZeroCopyBindings final {
 public:
  AndroidNativeZeroCopyBindings(AndroidNativeInteropConfig,
                                AndroidVulkanRgba16fDispatch,
                                AndroidGlesRgba16fDispatch,
                                AndroidMediaCodecSurfaceSubmit,
                                AndroidVulkanImport native_vulkan_import = {},
                                AndroidGlesImport native_gles_import = {});
  ~AndroidNativeZeroCopyBindings();

  AndroidNativeZeroCopyBindings(const AndroidNativeZeroCopyBindings&) = delete;
  AndroidNativeZeroCopyBindings& operator=(const AndroidNativeZeroCopyBindings&) = delete;

  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult import_vulkan(
      const AndroidHardwareBufferFrame&, AndroidImportedImage&) noexcept;
  [[nodiscard]] DigitorResult import_gles(
      const AndroidHardwareBufferFrame&, AndroidImportedImage&) noexcept;
  [[nodiscard]] DigitorResult convert(
      const AndroidImportedImage&, ProcessedGpuFramePtr&) noexcept;
  [[nodiscard]] DigitorResult submit_encoder(
      const ProcessedGpuFramePtr&) noexcept;

  [[nodiscard]] AndroidZeroCopyBinding binding(
      AndroidMediaCodecAcquire acquire,
      AndroidGpuFrameConsumer preview_consumer);
  [[nodiscard]] AndroidNativeInteropTelemetry telemetry() const;
  [[nodiscard]] bool gpu_only() const noexcept;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace digitor
