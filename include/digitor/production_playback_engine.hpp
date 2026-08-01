#pragma once

#include "digitor/gpu_frame.hpp"
#include "digitor/playback_transport.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <jthread>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace digitor {

enum class PlaybackQuality : std::uint32_t { full = 0, half = 1, quarter = 2, proxy = 3 };
enum class PlaybackPressure : std::uint32_t { normal = 0, elevated = 1, critical = 2 };

struct ProductionPlaybackConfig {
  std::int64_t duration_us{};
  double target_fps{30.0};
  std::size_t prefetch_frames{8};
  std::size_t maximum_queued_frames{16};
  std::uint64_t gpu_memory_budget_bytes{512ull * 1024ull * 1024ull};
  std::int64_t late_frame_threshold_us{40000};
  bool adaptive_quality{true};
  bool require_gpu_frames{true};
};

struct ProductionPlaybackFrame {
  ProcessedGpuFramePtr frame;
  std::int64_t pts_us{};
  std::int64_t duration_us{};
  std::uint64_t seek_generation{};
  std::uint64_t estimated_bytes{};
  PlaybackQuality quality{PlaybackQuality::full};
};

struct ProductionPlaybackTelemetry {
  PlaybackTransportSnapshot transport;
  PlaybackQuality quality{PlaybackQuality::full};
  PlaybackPressure memory_pressure{PlaybackPressure::normal};
  PlaybackPressure thermal_pressure{PlaybackPressure::normal};
  std::size_t queued_frames{};
  std::uint64_t queued_bytes{};
  std::uint64_t decoded_frames{};
  std::uint64_t presented_frames{};
  std::uint64_t dropped_frames{};
  std::uint64_t stale_frames{};
  std::uint64_t decode_failures{};
  std::uint64_t presentation_failures{};
  std::uint64_t cpu_frame_rejections{};
  std::uint64_t seek_requests{};
  std::uint64_t coalesced_seeks{};
  double average_decode_ms{};
  double average_present_ms{};
  std::int64_t last_presented_pts_us{-1};
  std::string last_error;
};

using PlaybackDecodeCallback = std::function<std::optional<ProductionPlaybackFrame>(
    std::int64_t pts_us, PlaybackQuality quality, std::uint64_t seek_generation)>;
using PlaybackPresentCallback = std::function<DigitorResult(const ProductionPlaybackFrame&)>;

class ProductionPlaybackEngine final {
 public:
  ProductionPlaybackEngine(ProductionPlaybackConfig config,
                           PlaybackDecodeCallback decode,
                           PlaybackPresentCallback present);
  ~ProductionPlaybackEngine();

  ProductionPlaybackEngine(const ProductionPlaybackEngine&) = delete;
  ProductionPlaybackEngine& operator=(const ProductionPlaybackEngine&) = delete;

  void play(std::int64_t monotonic_now_us);
  void pause(std::int64_t monotonic_now_us);
  void stop();
  void seek(std::int64_t position_us, std::int64_t monotonic_now_us);
  [[nodiscard]] bool set_rate(double rate, std::int64_t monotonic_now_us);
  [[nodiscard]] DigitorResult tick(std::int64_t monotonic_now_us);
  [[nodiscard]] std::int64_t update_audio_clock(std::int64_t raw_audio_clock_us,
                                                std::int64_t monotonic_now_us);
  void notify_audio_device_changed();
  [[nodiscard]] bool refresh_audio_device(std::int64_t monotonic_now_us);
  void set_memory_pressure(PlaybackPressure pressure);
  void set_thermal_pressure(PlaybackPressure pressure);
  void set_proxy_available(bool available) noexcept;
  [[nodiscard]] ProductionPlaybackTelemetry telemetry(std::int64_t monotonic_now_us) const;

 private:
  void worker_loop(std::stop_token token);
  void flush_for_generation(std::uint64_t generation);
  void update_quality_locked();
  [[nodiscard]] bool frame_is_acceptable(const ProductionPlaybackFrame& frame) const noexcept;
  [[nodiscard]] std::int64_t frame_interval_us() const noexcept;

  ProductionPlaybackConfig config_;
  PlaybackDecodeCallback decode_;
  PlaybackPresentCallback present_;
  PlaybackTransport transport_;

  mutable std::mutex mutex_;
  std::condition_variable_any wake_;
  std::deque<ProductionPlaybackFrame> queue_;
  std::jthread worker_;
  bool shutdown_{};
  bool playing_{};
  bool proxy_available_{};
  PlaybackQuality quality_{PlaybackQuality::full};
  PlaybackPressure memory_pressure_{PlaybackPressure::normal};
  PlaybackPressure thermal_pressure_{PlaybackPressure::normal};
  std::uint64_t queued_bytes_{};
  std::uint64_t generation_{};
  std::int64_t requested_position_us_{};
  std::int64_t next_decode_pts_us_{};
  ProductionPlaybackTelemetry counters_;
};

}  // namespace digitor
