#pragma once

#include "digitor/android_zero_copy_pipeline.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

struct AndroidMediaCodecDecoderConfig {
  std::string mime;
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t bit_depth{8};
  void* output_surface{}; // ANativeWindow*
  bool require_hardware_codec{true};
  bool require_ahardwarebuffer{true};
};

using AndroidCodecSurfaceAcquire = std::function<DigitorResult(
    std::int64_t timestamp_us, void*& hardware_buffer, int& acquire_fence_fd,
    std::shared_ptr<void>& lifetime)>;

class AndroidMediaCodecSurfaceDecoder final {
public:
  AndroidMediaCodecSurfaceDecoder(AndroidMediaCodecDecoderConfig,
                                  AndroidCodecSurfaceAcquire);
  ~AndroidMediaCodecSurfaceDecoder();
  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult acquire(std::int64_t timestamp_us,
                                      AndroidHardwareBufferFrame&) noexcept;
  [[nodiscard]] AndroidMediaCodecAcquire callback();
private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

struct AndroidVulkanExternalImportConfig {
  void* instance{};        // VkInstance
  void* physical_device{}; // VkPhysicalDevice
  void* device{};          // VkDevice
  void* queue{};           // VkQueue
  std::uint32_t queue_family{};
  bool require_sampler_ycbcr_conversion{true};
  bool require_external_fence_fd{true};
};

class AndroidVulkanHardwareBufferImporter final {
public:
  explicit AndroidVulkanHardwareBufferImporter(AndroidVulkanExternalImportConfig);
  ~AndroidVulkanHardwareBufferImporter();
  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult import(const AndroidHardwareBufferFrame&,
                                     AndroidImportedImage&) noexcept;
  [[nodiscard]] AndroidVulkanImport callback();
private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

struct AndroidGlesExternalImageConfig {
  void* egl_display{}; // EGLDisplay
  void* egl_context{}; // EGLContext
  bool require_native_fence_sync{true};
};

class AndroidGlesHardwareBufferImporter final {
public:
  explicit AndroidGlesHardwareBufferImporter(AndroidGlesExternalImageConfig);
  ~AndroidGlesHardwareBufferImporter();
  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult import(const AndroidHardwareBufferFrame&,
                                     AndroidImportedImage&) noexcept;
  [[nodiscard]] AndroidGlesImport callback();
private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

using AndroidImportedYuvDispatch = std::function<DigitorResult(
    const AndroidImportedImage&, AndroidColorMatrix, AndroidColorRange,
    std::uint32_t bit_depth, ProcessedGpuFramePtr&)>;

class AndroidGpuYuvConverter final {
public:
  explicit AndroidGpuYuvConverter(AndroidImportedYuvDispatch);
  [[nodiscard]] DigitorResult convert(const AndroidImportedImage&,
                                      ProcessedGpuFramePtr&) noexcept;
  [[nodiscard]] AndroidYuvToRgba16f callback();
private:
  AndroidImportedYuvDispatch dispatch_;
};

struct AndroidHardwareEncoderConfig {
  std::string mime{"video/hevc"};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t fps{30};
  std::uint32_t bitrate{20000000};
  std::uint32_t bit_depth{10};
  void* input_surface{}; // ANativeWindow*
};

using AndroidEncoderSubmit = std::function<DigitorResult(
    const ProcessedGpuFramePtr&, void* encoder_surface)>;

class AndroidMediaCodecHardwareEncoder final {
public:
  AndroidMediaCodecHardwareEncoder(AndroidHardwareEncoderConfig,
                                   AndroidEncoderSubmit);
  ~AndroidMediaCodecHardwareEncoder();
  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult consume(const ProcessedGpuFramePtr&) noexcept;
  [[nodiscard]] AndroidGpuFrameConsumer callback();
  [[nodiscard]] DigitorResult flush() noexcept;
private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

struct AndroidConcretePipelineBinding {
  AndroidMediaCodecDecoderConfig decoder;
  AndroidCodecSurfaceAcquire decoder_surface_acquire;
  AndroidVulkanExternalImportConfig vulkan;
  AndroidGlesExternalImageConfig gles;
  AndroidImportedYuvDispatch yuv_dispatch;
  AndroidGpuFrameConsumer preview_consumer;
  AndroidHardwareEncoderConfig encoder;
  AndroidEncoderSubmit encoder_submit;
};

class AndroidConcreteZeroCopyPipeline final {
public:
  AndroidConcreteZeroCopyPipeline(AndroidZeroCopyConfig,
                                  AndroidConcretePipelineBinding);
  ~AndroidConcreteZeroCopyPipeline();
  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult preview(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult export_frame(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult preview_and_export(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] AndroidZeroCopyTelemetry telemetry() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace digitor
