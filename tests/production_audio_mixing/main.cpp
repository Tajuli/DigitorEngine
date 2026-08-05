#include "digitor/production_audio_mixing.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

int main() {
  using namespace digitor;

  AudioBuffer left{48000u, 2u, {0.5f, 0.5f, 0.5f, 0.5f}};
  AudioBuffer right{48000u, 2u, {0.25f, 0.25f, 0.25f, 0.25f}};
  std::vector<AudioBuffer> tracks{left, right};
  std::vector<AudioTrackSettings> settings(2u);
  settings[0].pan = -1.0f;
  settings[1].pan = 1.0f;

  AudioMasterSettings master;
  AudioBuffer preview;
  AudioBuffer export_audio;
  const auto preview_result =
      mix_audio_reference(tracks, settings, preview, master);
  const auto export_result =
      mix_audio_reference(tracks, settings, export_audio, master);
  if (preview_result.status != AudioMixStatus::ready ||
      export_result.status != AudioMixStatus::ready) {
    return 1;
  }
  if (preview_result.metrics.digest != export_result.metrics.digest) {
    return 2;
  }
  if (preview.samples.size() != 4u || preview.samples[0] <= 0.0f ||
      preview.samples[1] <= 0.0f) {
    return 3;
  }
  if (preview_result.metrics.peak_dbfs > 0.01f ||
      !std::isfinite(preview_result.metrics.integrated_lufs)) {
    return 4;
  }

  settings[0].mute = true;
  AudioBuffer muted;
  if (mix_audio_reference(tracks, settings, muted, master).status !=
      AudioMixStatus::ready) {
    return 5;
  }
  if (muted.samples[0] != 0.0f) {
    return 6;
  }

  settings[0].mute = false;
  settings[0].fade_in_seconds = 1.0f / 48000.0f;
  AudioBuffer faded;
  if (mix_audio_reference(tracks, settings, faded, master).status !=
      AudioMixStatus::ready) {
    return 7;
  }
  if (faded.samples[0] != 0.0f) {
    return 8;
  }

  const float* packed_tracks[2] = {left.samples.data(), right.samples.data()};
  const std::uint64_t frame_counts[2] = {2u, 2u};
  DigitorAudioTrackSettings c_tracks[2]{};
  c_tracks[0].pan = -1.0f;
  c_tracks[1].pan = 1.0f;
  DigitorAudioMasterSettings c_master{};
  c_master.limiter_ceiling_db = -1.0f;
  c_master.limiter_release_ms = 80.0f;
  c_master.limiter_enabled = 1u;
  std::vector<float> packed_output(4u);
  DigitorAudioMixMetrics metrics{};
  if (digitor_mix_audio_f32(packed_tracks, frame_counts, c_tracks, 2u,
                            48000u, 2u, packed_output.data(), 2u,
                            &c_master, &metrics) != 0u) {
    return 9;
  }
  if (metrics.digest == 0u || packed_output[0] <= 0.0f ||
      packed_output[1] <= 0.0f) {
    return 10;
  }

  AudioBuffer invalid{0u, 2u, {0.0f, 0.0f}};
  AudioBuffer invalid_output;
  if (mix_audio_reference({invalid}, {AudioTrackSettings{}}, invalid_output,
                          master).status != AudioMixStatus::invalid) {
    return 11;
  }

  return 0;
}
