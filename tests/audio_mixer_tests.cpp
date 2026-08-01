#include "digitor/audio_mixer.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace digitor;

namespace {

AudioMixerConfig config(std::int64_t frames = 8) {
    AudioMixerConfig value;
    value.sample_rate = 48000;
    value.channels = 2;
    value.output_start_sample = 100;
    value.output_sample_count = frames;
    value.enable_soft_limiter = true;
    return value;
}

AudioPcmBlock mono(std::uint64_t id, std::int64_t start,
                   std::initializer_list<float> samples) {
    AudioPcmBlock block;
    block.clip_id = id;
    block.sample_rate = 48000;
    block.channels = 1;
    block.destination_start_sample = start;
    block.samples = samples;
    return block;
}

void test_sample_accurate_overlap() {
    const auto result = mix_audio_blocks(config(4),
        {mono(1, 100, {0.25f, 0.25f, 0.25f, 0.25f}),
         mono(2, 102, {0.25f, 0.25f})}, {});
    assert(result.status == AudioMixStatus::ok);
    assert(result.samples.size() == 8);
    assert(result.samples[0] > 0.17f && result.samples[0] < 0.18f);
    assert(result.samples[4] > result.samples[0]);
}

void test_mute_and_solo() {
    AudioTrackMix muted;
    muted.clip_id = 1;
    muted.muted = true;
    AudioTrackMix solo;
    solo.clip_id = 2;
    solo.solo = true;
    const auto result = mix_audio_blocks(config(2),
        {mono(1, 100, {0.8f, 0.8f}), mono(2, 100, {0.2f, 0.2f})},
        {muted, solo});
    assert(result.status == AudioMixStatus::ok);
    assert(result.samples[0] > 0.13f && result.samples[0] < 0.15f);
}

void test_gain_pan_and_fades() {
    AudioTrackMix track;
    track.clip_id = 3;
    track.gain_db = -6.020599913279624;
    track.pan = -1.0;
    track.fade_in_samples = 2;
    track.fade_out_samples = 2;
    const auto result = mix_audio_blocks(config(4),
        {mono(3, 100, {1.0f, 1.0f, 1.0f, 1.0f})}, {track});
    assert(result.status == AudioMixStatus::ok);
    assert(std::abs(result.samples[1]) < 1e-6f);
    assert(result.samples[0] == 0.0f);
    assert(result.samples[2] > 0.24f);
    assert(result.samples[6] > 0.24f);
}

void test_limiter_and_meters() {
    auto value = config(2);
    value.master_gain_db = 12.0;
    const auto result = mix_audio_blocks(value,
        {mono(4, 100, {1.0f, 1.0f})}, {});
    assert(result.status == AudioMixStatus::ok);
    assert(result.clipped_sample_count > 0);
    assert(result.meters.size() == 2);
    assert(result.meters[0].peak <= 1.0f);
    assert(result.meters[0].rms > 0.0f);
}

void test_preview_export_identity() {
    AudioTrackMix track;
    track.clip_id = 5;
    track.gain_db = -3.0;
    track.pan = 0.25;
    const auto blocks = std::vector<AudioPcmBlock>{
        mono(5, 100, {0.1f, -0.2f, 0.3f, -0.4f})};
    const auto preview = mix_audio_preview(config(4), blocks, {track});
    const auto exported = mix_audio_export(config(4), blocks, {track});
    assert(preview.status == AudioMixStatus::ok);
    assert(preview.samples == exported.samples);
    assert(preview.clipped_sample_count == exported.clipped_sample_count);
}

void test_validation() {
    auto value = config(2);
    value.channels = 0;
    std::string diagnostic;
    assert(validate_audio_mix(value, {}, {}, diagnostic) ==
           AudioMixStatus::invalid_configuration);

    AudioTrackMix invalid;
    invalid.clip_id = 1;
    invalid.pan = 2.0;
    assert(validate_audio_mix(config(2), {mono(1, 100, {0.1f})},
                              {invalid}, diagnostic) ==
           AudioMixStatus::invalid_input);
}

} // namespace

int main() {
    test_sample_accurate_overlap();
    test_mute_and_solo();
    test_gain_pan_and_fades();
    test_limiter_and_meters();
    test_preview_export_identity();
    test_validation();
    std::cout << "production audio mixer: PASS\n";
    return 0;
}
