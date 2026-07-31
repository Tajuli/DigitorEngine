#pragma once

#include "digitor/digitor.h"
#include "digitor/gpu_frame.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

enum class AppleYuvFormat : std::uint32_t { nv12_video=1, nv12_full=2, p010_video=3, p010_full=4 };
enum class AppleColorMatrix : std::uint32_t { bt601=1, bt709=2, bt2020_ncl=3 };

struct ApplePixelBufferFrame {
  void* pixel_buffer{}; // CVPixelBufferRef
  AppleYuvFormat format{AppleYuvFormat::nv12_video};
  AppleColorMatrix matrix{AppleColorMatrix::bt709};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t bit_depth{8};
  std::int64_t timestamp_us{};
  std::uint64_t frame_identity{};
  std::shared_ptr<void> decoder_lifetime;
};

struct AppleMetalImportedFrame {
  void* luma_texture{};   // id<MTLTexture>
  void* chroma_texture{}; // id<MTLTexture>
  void* command_buffer{}; // id<MTLCommandBuffer>
  std::uint32_t width{};
  std::uint32_t height{};
  AppleYuvFormat format{AppleYuvFormat::nv12_video};
  AppleColorMatrix matrix{AppleColorMatrix::bt709};
  std::int64_t timestamp_us{};
  std::uint64_t frame_identity{};
  std::shared_ptr<void> lifetime;
};

using AppleVideoToolboxAcquire = std::function<DigitorResult(std::int64_t, ApplePixelBufferFrame&)>;
using AppleMetalImport = std::function<DigitorResult(const ApplePixelBufferFrame&, AppleMetalImportedFrame&)>;
using AppleYuvToRgba16f = std::function<DigitorResult(const AppleMetalImportedFrame&, ProcessedGpuFramePtr&)>;
using AppleGpuConsumer = std::function<DigitorResult(const ProcessedGpuFramePtr&)>;

struct AppleZeroCopyBinding {
  AppleVideoToolboxAcquire acquire_decoder_frame;
  AppleMetalImport import_metal;
  AppleYuvToRgba16f convert_to_rgba16f;
  AppleGpuConsumer preview_consumer;
  AppleGpuConsumer encoder_consumer;
};

struct AppleZeroCopyConfig {
  bool strict_gpu_first{true};
  bool require_iosurface{true};
  bool require_rgba16f_output{true};
  bool require_preview_export_identity{true};
  std::uint32_t max_in_flight_frames{6};
  std::uint32_t frame_timeout_ms{250};
  std::string device_registry_id;
  std::string os_build;
  std::string engine_commit;
};

struct AppleZeroCopyTelemetry {
  std::uint64_t frames_requested{};
  std::uint64_t pixel_buffers{};
  std::uint64_t metal_imports{};
  std::uint64_t rgba16f_frames{};
  std::uint64_t preview_frames{};
  std::uint64_t encoder_frames{};
  std::uint64_t failures{};
  std::uint64_t cpu_copies{};
  std::uint64_t cpu_fallback_frames{};
  bool quarantined{};
  std::string diagnostic;
};

class AppleZeroCopyPipeline final {
public:
  AppleZeroCopyPipeline(AppleZeroCopyConfig, AppleZeroCopyBinding);
  ~AppleZeroCopyPipeline();
  AppleZeroCopyPipeline(const AppleZeroCopyPipeline&) = delete;
  AppleZeroCopyPipeline& operator=(const AppleZeroCopyPipeline&) = delete;

  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult preview(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult export_frame(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult preview_and_export(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult reset_quarantine() noexcept;
  [[nodiscard]] AppleZeroCopyTelemetry telemetry() const;
  [[nodiscard]] bool production_active() const noexcept;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace digitor
