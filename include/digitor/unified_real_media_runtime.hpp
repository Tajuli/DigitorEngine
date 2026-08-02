#pragma once

#include "digitor/production_hardware_decode.hpp"
#include "digitor/production_playback_engine.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace digitor {

struct UnifiedRealMediaRuntimeConfig {
  ProductionPlaybackConfig playback;
  std::uint64_t default_estimated_frame_bytes{};
};

struct UnifiedNativeTextureDescriptor {
  DigitorRendererBackend backend{DIGITOR_RENDERER_AUTO};
  DigitorPixelFormat format{DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT};
  std::uint32_t width{};
  std::uint32_t height{};
  std::int64_t timestamp_us{};
  std::uint64_t frame_identity{};
  std::uint64_t generation{};
};

using TimelineFrameResolver = std::function<std::optional<FrameNumber>(
    std::int64_t timeline_pts_us, PlaybackQuality quality)>;
using ExistingGpuPipeline = std::function<DigitorResult(
    const ProductionDecodedFrame&, PlaybackQuality, ProcessedGpuFramePtr&)>;
using NativeFlutterPresenter = std::function<DigitorResult(
    const ProcessedGpuFramePtr&, const UnifiedNativeTextureDescriptor&)>;

class UnifiedRealMediaRuntime final {
 public:
  UnifiedRealMediaRuntime(std::unique_ptr<ProductionHardwareDecodeSession> decode,
                          UnifiedRealMediaRuntimeConfig config,
                          TimelineFrameResolver resolver,
                          NativeFlutterPresenter presenter,
                          ExistingGpuPipeline pipeline = {});
  ~UnifiedRealMediaRuntime();

  UnifiedRealMediaRuntime(const UnifiedRealMediaRuntime&) = delete;
  UnifiedRealMediaRuntime& operator=(const UnifiedRealMediaRuntime&) = delete;

  void play(std::int64_t monotonic_now_us);
  void pause(std::int64_t monotonic_now_us);
  void stop();
  [[nodiscard]] DigitorResult seek(std::int64_t timeline_pts_us,
                                   std::int64_t monotonic_now_us,
                                   std::string* diagnostic = nullptr) noexcept;
  [[nodiscard]] DigitorResult scrub(std::int64_t timeline_pts_us,
                                    std::int64_t monotonic_now_us,
                                    std::string* diagnostic = nullptr) noexcept;
  void step_frames(std::int64_t frame_count, std::int64_t monotonic_now_us);
  [[nodiscard]] bool set_rate(double rate, std::int64_t monotonic_now_us);
  [[nodiscard]] DigitorResult tick(std::int64_t monotonic_now_us);
  [[nodiscard]] std::int64_t update_audio_clock(std::int64_t raw_audio_clock_us,
                                                std::int64_t monotonic_now_us);
  void set_memory_pressure(PlaybackPressure pressure);
  void set_thermal_pressure(PlaybackPressure pressure);
  void set_proxy_available(bool available) noexcept;

  [[nodiscard]] ProductionPlaybackTelemetry telemetry(std::int64_t monotonic_now_us) const;
  [[nodiscard]] HardwareDecodeQualification decode_qualification() const;
  [[nodiscard]] std::optional<UnifiedNativeTextureDescriptor> last_native_texture() const;

 private:
  std::optional<ProductionPlaybackFrame> decode_for_playback(
      std::int64_t pts_us, PlaybackQuality quality, std::uint64_t generation);
  DigitorResult present_for_flutter(const ProductionPlaybackFrame& frame) noexcept;
  static std::uint64_t estimate_bytes(const GpuFrameMetadata& metadata,
                                      std::uint64_t fallback) noexcept;

  std::unique_ptr<ProductionHardwareDecodeSession> decode_;
  TimelineFrameResolver resolver_;
  ExistingGpuPipeline pipeline_;
  NativeFlutterPresenter presenter_;
  std::unique_ptr<ProductionPlaybackEngine> playback_;
  std::uint64_t default_estimated_frame_bytes_{};

  mutable std::mutex mutex_;
  std::optional<UnifiedNativeTextureDescriptor> last_texture_;
  std::uint64_t presentation_generation_{};
};

}  // namespace digitor
