#pragma once

#include "digitor/apple_zero_copy_pipeline.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

struct AppleNativeZeroCopyConfig {
  void* metal_device{};          // id<MTLDevice>
  void* command_queue{};         // id<MTLCommandQueue>
  void* compression_session{};   // VTCompressionSessionRef
  bool require_hardware_encoder{true};
  bool require_p010_for_10bit{true};
  std::uint32_t frame_duration_us{33333};
};

struct AppleNativeZeroCopyTelemetry {
  std::uint64_t texture_cache_imports{};
  std::uint64_t nv12_imports{};
  std::uint64_t p010_imports{};
  std::uint64_t compute_dispatches{};
  std::uint64_t encoder_submissions{};
  std::uint64_t failures{};
  std::uint64_t cpu_copies{};
  std::string diagnostic;
};

using AppleMetalRgba16fDispatch = std::function<DigitorResult(
    const AppleMetalImportedFrame&, void* command_queue,
    ProcessedGpuFramePtr&)>;

// Produces an IOSurface-backed encoder CVPixelBuffer from the graded Metal
// frame. The implementation must remain GPU-only and preserve identity/time.
using AppleEncoderPixelBufferAcquire = std::function<DigitorResult(
    const ProcessedGpuFramePtr&, void*& pixel_buffer)>;

class AppleNativeZeroCopyBindings final {
public:
  AppleNativeZeroCopyBindings(AppleNativeZeroCopyConfig,
                              AppleMetalRgba16fDispatch,
                              AppleEncoderPixelBufferAcquire);
  ~AppleNativeZeroCopyBindings();
  AppleNativeZeroCopyBindings(const AppleNativeZeroCopyBindings&) = delete;
  AppleNativeZeroCopyBindings& operator=(const AppleNativeZeroCopyBindings&) = delete;

  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult import_metal(
      const ApplePixelBufferFrame&, AppleMetalImportedFrame&) noexcept;
  [[nodiscard]] DigitorResult convert(
      const AppleMetalImportedFrame&, ProcessedGpuFramePtr&) noexcept;
  [[nodiscard]] DigitorResult submit_encoder(
      const ProcessedGpuFramePtr&) noexcept;
  [[nodiscard]] AppleZeroCopyBinding binding(
      AppleVideoToolboxAcquire, AppleGpuConsumer preview_consumer);
  [[nodiscard]] AppleNativeZeroCopyTelemetry telemetry() const;
  [[nodiscard]] bool gpu_only() const noexcept;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace digitor
