#include "digitor/media.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
  hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
  return hash;
}

std::uint64_t hash_frame(const digitor::VideoFrame& frame) {
  std::uint64_t hash = 1469598103934665603ULL;
  hash = mix(hash, static_cast<std::uint64_t>(frame.pts));
  hash = mix(hash, static_cast<std::uint64_t>(frame.duration));
  const std::uint32_t xs[] = {frame.width / 4U, frame.width / 2U, (frame.width * 3U) / 4U};
  const std::uint32_t ys[] = {frame.height / 4U, frame.height / 2U, (frame.height * 3U) / 4U};
  for (const auto y : ys) {
    for (const auto x : xs) {
      const auto& pixel = frame.pixels[static_cast<std::size_t>(y) * frame.width + x];
      hash = mix(hash, static_cast<std::uint64_t>(std::lround(pixel.r * 65535.0F)));
      hash = mix(hash, static_cast<std::uint64_t>(std::lround(pixel.g * 65535.0F)));
      hash = mix(hash, static_cast<std::uint64_t>(std::lround(pixel.b * 65535.0F)));
      hash = mix(hash, static_cast<std::uint64_t>(std::lround(pixel.a * 65535.0F)));
    }
  }
  return hash;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return fail("expected VFR fixture path");
  if (!digitor::ffmpeg_available()) return fail("FFmpeg support unavailable");

  digitor::DecoderOptions options;
  options.hardware = digitor::HardwareDecode::cpu;
  options.allow_cpu_fallback = true;
  options.cache_capacity = 64;

  auto preview = digitor::open_video_decoder(argv[1], options);
  auto export_path = digitor::open_video_decoder(argv[1], options);
  if (!preview || !export_path) return fail("decoder creation failed");

  std::vector<std::int64_t> durations;
  std::int64_t previous_pts = -1;
  std::uint64_t preview_hash = 1469598103934665603ULL;
  std::uint64_t export_hash = 1469598103934665603ULL;

  for (digitor::FrameNumber index = 0; index < 45; ++index) {
    const auto preview_frame = preview->decode(index);
    const auto export_frame = export_path->decode(index);
    if (!preview_frame || !export_frame) return fail("unexpected VFR video EOF");
    if (!preview_frame->cpu_resident() || !export_frame->cpu_resident()) return fail("missing decoded pixels");
    if (preview_frame->pts < previous_pts) return fail("VFR PTS is not monotonic");
    if (preview_frame->pts != export_frame->pts || preview_frame->duration != export_frame->duration)
      return fail("preview/export timestamps diverged");
    previous_pts = preview_frame->pts;
    if (preview_frame->duration > 0) durations.push_back(preview_frame->duration);
    preview_hash = mix(preview_hash, hash_frame(*preview_frame));
    export_hash = mix(export_hash, hash_frame(*export_frame));
  }

  if (preview_hash != export_hash) return fail("preview/export pixel hash mismatch");
  bool saw_duration_change = false;
  for (std::size_t index = 1; index < durations.size(); ++index) {
    if (durations[index] != durations[0]) {
      saw_duration_change = true;
      break;
    }
  }
  if (!saw_duration_change) return fail("fixture did not expose variable frame durations");

  preview->seek(900000);
  export_path->seek(900000);
  const auto preview_seek = preview->decode(0);
  const auto export_seek = export_path->decode(0);
  if (!preview_seek || !export_seek) return fail("seek decode failed");
  if (preview_seek->pts < 800000 || export_seek->pts != preview_seek->pts)
    return fail("preview/export seek parity failed");
  if (hash_frame(*preview_seek) != hash_frame(*export_seek)) return fail("seeked pixel parity failed");

  return 0;
}
