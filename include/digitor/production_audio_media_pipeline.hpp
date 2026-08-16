#pragma once

#include "digitor/digitor.h"
#include "digitor/professional_audio_engine.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace digitor {

struct ProductionAudioPipelineAcquireResult final {
  std::shared_ptr<class ProductionAudioMediaPipeline> pipeline;
  DigitorResult result{DIGITOR_RESULT_NOT_INITIALIZED};
  bool no_audio_stream{};
  std::string diagnostic;

  [[nodiscard]] explicit operator bool() const noexcept {
    return result == DIGITOR_RESULT_OK && static_cast<bool>(pipeline);
  }
};

// Canonical engine-owned audio path for a media source. Playback and export
// call the same ProfessionalAudioEngine with the same source decoder and the
// same immutable audio snapshot/effect graph. Only their terminal sinks differ.
class ProductionAudioMediaPipeline final {
 public:
  ~ProductionAudioMediaPipeline();

  ProductionAudioMediaPipeline(const ProductionAudioMediaPipeline&) = delete;
  ProductionAudioMediaPipeline& operator=(const ProductionAudioMediaPipeline&) = delete;

  [[nodiscard]] std::uint32_t sample_rate() const noexcept;
  [[nodiscard]] std::uint32_t channels() const noexcept;
  [[nodiscard]] std::uint64_t revision() const noexcept;
  [[nodiscard]] std::int64_t duration_us() const noexcept;

  [[nodiscard]] DigitorResult publish_single_source_snapshot(
      std::uint64_t revision, std::int64_t duration_us,
      double master_gain_db, bool enable_dynamics,
      std::string* diagnostic = nullptr) noexcept;

  // Export freezes the current audio snapshot revision. While the lock is
  // held, UI/control publication is rejected instead of mutating an in-flight
  // export. Playback may continue and is rendered from that same snapshot.
  [[nodiscard]] DigitorResult begin_export_revision(
      std::uint64_t revision, std::string* diagnostic = nullptr) noexcept;
  void end_export_revision() noexcept;

  [[nodiscard]] DigitorResult render_playback(
      std::int64_t timeline_start_us, std::uint32_t frame_count,
      AudioPlaybackSink sink, bool* out_had_source_audio = nullptr,
      std::string* diagnostic = nullptr) noexcept;

  [[nodiscard]] DigitorResult render_export(
      std::int64_t timeline_start_us, std::uint32_t frame_count,
      AudioExportSink sink, bool* out_had_source_audio = nullptr,
      std::string* diagnostic = nullptr) noexcept;

  [[nodiscard]] ProfessionalAudioTelemetry telemetry() const;
  [[nodiscard]] static constexpr std::uint32_t maximum_block_frames() noexcept {
    return 2048;
  }

 private:
  struct Impl;
  explicit ProductionAudioMediaPipeline(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;

  friend ProductionAudioPipelineAcquireResult
  acquire_production_audio_media_pipeline(const std::string&) noexcept;
};

// Per-path weak registry. Timeline preview and production export acquire the
// same live pipeline object, so decode + automation + effects are canonical and
// sinks cannot silently create independent processing graphs.
[[nodiscard]] ProductionAudioPipelineAcquireResult
acquire_production_audio_media_pipeline(const std::string& media_path) noexcept;

}  // namespace digitor
