#include "digitor/professional_audio_engine.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace digitor;

namespace {
struct Capture {
  std::vector<float> playback;
  std::vector<float> exported;
};

std::shared_ptr<ProfessionalAudioSnapshot> snapshot() {
  auto value = std::make_shared<ProfessionalAudioSnapshot>();
  value->revision = 42;
  value->sample_rate = 48000;
  value->layout = AudioChannelLayout::stereo;
  value->master_bus_id = 1;

  AudioTrackState track;
  track.track_id = 7;
  track.bus_id = 1;
  track.gain_db = 0.0f;
  track.pan = 0.0f;
  track.volume_automation.default_value = 0.0f;
  track.volume_automation.points = {{0, -6.0f, AudioAutomationCurve::linear},
                                    {1000000, 0.0f, AudioAutomationCurve::linear}};
  track.pan_automation.default_value = 0.0f;
  AudioClipState clip;
  clip.clip_id = 9;
  clip.timeline_in_us = 0;
  clip.timeline_out_us = 2000000;
  clip.source_in_us = 0;
  clip.fade_in_us = 10000;
  clip.fade_out_us = 10000;
  track.clips.push_back(clip);
  value->tracks.push_back(track);

  AudioBusState master;
  master.bus_id = 1;
  AudioEffectParameters limiter;
  limiter.type = AudioEffectType::limiter;
  limiter.ceiling_db = -1.0f;
  master.effects.push_back(limiter);
  value->buses.push_back(master);
  return value;
}
}  // namespace

int main() {
  Capture capture;
  auto source = [](const AudioClipState&, std::int64_t, std::uint32_t,
                   AudioBufferView output, std::string& error) {
    for (std::uint32_t c = 0; c < output.channel_count; ++c)
      for (std::uint32_t i = 0; i < output.frame_count; ++i)
        output.channels[c][i] = 0.5f;
    error.clear();
    return DIGITOR_RESULT_OK;
  };
  auto playback = [&](ConstAudioBufferView input, std::int64_t, std::string& error) {
    capture.playback.assign(input.channels[0], input.channels[0] + input.frame_count);
    error.clear();
    return DIGITOR_RESULT_OK;
  };
  auto exported = [&](ConstAudioBufferView input, std::int64_t, std::string& error) {
    capture.exported.assign(input.channels[0], input.channels[0] + input.frame_count);
    error.clear();
    return DIGITOR_RESULT_OK;
  };

  ProfessionalAudioConfig config;
  config.maximum_block_frames = 512;
  ProfessionalAudioEngine engine(config, source, playback, exported);
  std::string diagnostic;
  assert(engine.publish_snapshot(snapshot(), &diagnostic) == DIGITOR_RESULT_OK);
  assert(engine.render_playback(0, 480, &diagnostic) == DIGITOR_RESULT_OK);
  assert(engine.render_export(0, 480, &diagnostic) == DIGITOR_RESULT_OK);
  assert(capture.playback.size() == 480);
  assert(capture.playback == capture.exported);
  assert(std::abs(capture.playback.front()) < std::abs(capture.playback.back()));

  const auto telemetry = engine.telemetry();
  assert(telemetry.rendered_blocks == 2);
  assert(telemetry.rendered_frames == 960);
  assert(telemetry.snapshot_revision == 42);
  assert(telemetry.master_meter.peak_db <= 0.0f);
  assert(telemetry.last_error.empty());

  engine.notify_underrun();
  engine.notify_overrun();
  const auto pressure = engine.telemetry();
  assert(pressure.underruns == 1);
  assert(pressure.overruns == 1);

  auto invalid = snapshot();
  invalid->sample_rate = 0;
  assert(engine.publish_snapshot(invalid, &diagnostic) == DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(engine.render_playback(0, 1024, &diagnostic) == DIGITOR_RESULT_BACKEND_UNAVAILABLE);

  AudioAutomationLane lane;
  lane.default_value = 2.0f;
  lane.points = {{0, 0.0f, AudioAutomationCurve::linear},
                 {100, 1.0f, AudioAutomationCurve::linear}};
  assert(std::abs(lane.value_at(50) - 0.5f) < 0.0001f);
  return 0;
}
