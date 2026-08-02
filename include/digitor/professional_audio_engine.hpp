#pragma once

#include "digitor/digitor.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace digitor {

enum class AudioChannelLayout : std::uint32_t { mono = 1, stereo = 2, surround_5_1 = 6, surround_7_1 = 8 };
enum class AudioAutomationCurve : std::uint32_t { hold = 0, linear = 1 };
enum class AudioEffectType : std::uint32_t { gain = 0, equalizer = 1, compressor = 2, limiter = 3 };

struct AudioBufferView {
  float* const* channels{};
  std::uint32_t channel_count{};
  std::uint32_t frame_count{};
};

struct ConstAudioBufferView {
  const float* const* channels{};
  std::uint32_t channel_count{};
  std::uint32_t frame_count{};
};

struct AudioAutomationPoint {
  std::int64_t timeline_us{};
  float value{};
  AudioAutomationCurve curve{AudioAutomationCurve::linear};
};

struct AudioAutomationLane {
  std::vector<AudioAutomationPoint> points;
  float default_value{};
  [[nodiscard]] float value_at(std::int64_t timeline_us) const noexcept;
};

struct AudioEffectParameters {
  AudioEffectType type{AudioEffectType::gain};
  bool enabled{true};
  float gain_db{};
  float low_gain_db{};
  float mid_gain_db{};
  float high_gain_db{};
  float threshold_db{-18.0f};
  float ratio{4.0f};
  float attack_ms{10.0f};
  float release_ms{100.0f};
  float ceiling_db{-1.0f};
};

struct AudioClipState {
  std::uint64_t clip_id{};
  std::int64_t timeline_in_us{};
  std::int64_t timeline_out_us{};
  std::int64_t source_in_us{};
  float gain_db{};
  std::int64_t fade_in_us{};
  std::int64_t fade_out_us{};
  bool enabled{true};
};

struct AudioTrackState {
  std::uint64_t track_id{};
  std::uint64_t bus_id{};
  bool enabled{true};
  bool muted{};
  bool solo{};
  float gain_db{};
  float pan{};
  AudioAutomationLane volume_automation;
  AudioAutomationLane pan_automation;
  std::vector<AudioEffectParameters> effects;
  std::vector<AudioClipState> clips;
};

struct AudioBusState {
  std::uint64_t bus_id{};
  std::uint64_t output_bus_id{};
  bool enabled{true};
  bool muted{};
  float gain_db{};
  float pan{};
  std::vector<AudioEffectParameters> effects;
};

struct ProfessionalAudioSnapshot {
  std::uint64_t revision{};
  std::uint32_t sample_rate{48000};
  AudioChannelLayout layout{AudioChannelLayout::stereo};
  std::vector<AudioTrackState> tracks;
  std::vector<AudioBusState> buses;
  std::uint64_t master_bus_id{};
};

struct AudioMeter {
  float peak_db{-120.0f};
  float rms_db{-120.0f};
  float integrated_lufs{-120.0f};
  std::uint64_t clipped_samples{};
};

struct ProfessionalAudioTelemetry {
  std::uint64_t rendered_blocks{};
  std::uint64_t rendered_frames{};
  std::uint64_t source_failures{};
  std::uint64_t sink_failures{};
  std::uint64_t underruns{};
  std::uint64_t overruns{};
  std::uint64_t snapshot_revision{};
  double average_render_ms{};
  AudioMeter master_meter;
  std::string last_error;
};

using AudioSourceRender = std::function<DigitorResult(
    const AudioClipState&, std::int64_t source_start_us, std::uint32_t sample_rate,
    AudioBufferView destination, std::string& diagnostic)>;
using AudioPlaybackSink = std::function<DigitorResult(
    ConstAudioBufferView source, std::int64_t timeline_start_us, std::string& diagnostic)>;
using AudioExportSink = AudioPlaybackSink;

struct ProfessionalAudioConfig {
  std::uint32_t maximum_block_frames{2048};
  std::uint32_t maximum_tracks{128};
  std::uint32_t maximum_buses{32};
  bool require_realtime_safe_render{true};
};

class ProfessionalAudioEngine final {
 public:
  ProfessionalAudioEngine(ProfessionalAudioConfig config, AudioSourceRender source,
                          AudioPlaybackSink playback_sink = {}, AudioExportSink export_sink = {});
  ~ProfessionalAudioEngine();

  ProfessionalAudioEngine(const ProfessionalAudioEngine&) = delete;
  ProfessionalAudioEngine& operator=(const ProfessionalAudioEngine&) = delete;

  [[nodiscard]] DigitorResult publish_snapshot(
      std::shared_ptr<const ProfessionalAudioSnapshot> snapshot,
      std::string* diagnostic = nullptr) noexcept;
  [[nodiscard]] DigitorResult render_playback(
      std::int64_t timeline_start_us, std::uint32_t frame_count,
      std::string* diagnostic = nullptr) noexcept;
  [[nodiscard]] DigitorResult render_export(
      std::int64_t timeline_start_us, std::uint32_t frame_count,
      std::string* diagnostic = nullptr) noexcept;
  void notify_underrun() noexcept;
  void notify_overrun() noexcept;
  [[nodiscard]] ProfessionalAudioTelemetry telemetry() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace digitor
