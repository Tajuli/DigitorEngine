#include "digitor/media.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
  hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
  return hash;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return fail("expected fixture path");
  if (!digitor::ffmpeg_available()) return fail("FFmpeg support unavailable");

  const std::string path = argv[1];
  digitor::DecoderOptions options;
  options.hardware = digitor::HardwareDecode::cpu;
  options.allow_cpu_fallback = true;
  options.cache_capacity = 32;

  auto video = digitor::open_video_decoder(path, options);
  auto audio = digitor::open_audio_decoder(path, options);
  if (!video || !audio) return fail("decoder creation failed");

  std::int64_t previous_video_pts = -1;
  std::uint64_t video_hash = 1469598103934665603ULL;
  for (digitor::FrameNumber index = 0; index < 24; ++index) {
    const auto frame = video->decode(index);
    if (!frame) return fail("unexpected video EOF");
    if (frame->width != 320 || frame->height != 180) return fail("unexpected video dimensions");
    if (!frame->cpu_resident() || frame->pixels.size() != static_cast<std::size_t>(frame->width) * frame->height)
      return fail("video frame missing CPU pixels");
    if (frame->pts < previous_video_pts) return fail("video PTS is not monotonic");
    previous_video_pts = frame->pts;
    const auto& pixel = frame->pixels[(frame->height / 2U) * frame->width + frame->width / 2U];
    video_hash = mix(video_hash, static_cast<std::uint64_t>(std::lround(pixel.r * 65535.0F)));
    video_hash = mix(video_hash, static_cast<std::uint64_t>(std::lround(pixel.g * 65535.0F)));
    video_hash = mix(video_hash, static_cast<std::uint64_t>(std::lround(pixel.b * 65535.0F)));
  }

  std::int64_t previous_audio_pts = -1;
  std::uint64_t audio_samples = 0;
  for (digitor::FrameNumber index = 0; index < 20; ++index) {
    const auto frame = audio->decode(index);
    if (!frame) return fail("unexpected audio EOF");
    if (frame->sample_rate != 48000 || frame->channels == 0) return fail("unexpected audio format");
    if (frame->samples.empty()) return fail("audio frame missing samples");
    if (frame->pts < previous_audio_pts) return fail("audio PTS is not monotonic");
    previous_audio_pts = frame->pts;
    audio_samples += frame->samples.size() / frame->channels;
  }
  if (audio_samples < 10000) return fail("insufficient decoded audio samples");

  video->seek(500000);
  const auto seeked = video->decode(0);
  if (!seeked || seeked->pts < 400000) return fail("video seek did not reach requested region");

  auto repeat = digitor::open_video_decoder(path, options);
  std::uint64_t repeat_hash = 1469598103934665603ULL;
  for (digitor::FrameNumber index = 0; index < 24; ++index) {
    const auto frame = repeat->decode(index);
    if (!frame) return fail("unexpected repeated video EOF");
    const auto& pixel = frame->pixels[(frame->height / 2U) * frame->width + frame->width / 2U];
    repeat_hash = mix(repeat_hash, static_cast<std::uint64_t>(std::lround(pixel.r * 65535.0F)));
    repeat_hash = mix(repeat_hash, static_cast<std::uint64_t>(std::lround(pixel.g * 65535.0F)));
    repeat_hash = mix(repeat_hash, static_cast<std::uint64_t>(std::lround(pixel.b * 65535.0F)));
  }
  if (repeat_hash != video_hash) return fail("decoded video content is not deterministic");

  return 0;
}
