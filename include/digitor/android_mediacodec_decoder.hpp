#pragma once

#include "digitor/native_media.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace digitor {

enum class AndroidDecodeScheduling : std::uint32_t {
  frame_accurate,
  realtime_latest
};

struct AndroidMediaCodecSessionConfig {
  std::string media_path;
  std::uint32_t max_acquired_images{4};
  std::uint32_t dequeue_timeout_us{10000};
  AndroidDecodeScheduling scheduling{AndroidDecodeScheduling::frame_accurate};
  bool strict_zero_copy{true};
};

struct AndroidMediaCodecCapabilities {
  bool ndk_media_codec{};
  bool image_reader_private_output{};
  bool ahardwarebuffer{};
  bool gpu_sampled_output{};
  bool supports_avc{}, supports_hevc{}, supports_vp9{}, supports_av1{};
  bool supports_8_bit{}, supports_p010{};
  std::string output_handle_type;
  std::string unavailable_reason;
};

struct AndroidDecoderStatistics {
  std::uint64_t submitted_samples{};
  std::uint64_t rendered_outputs{};
  std::uint64_t acquired_images{};
  std::uint64_t dropped_preview_images{};
  std::uint64_t stale_generation_images{};
  std::uint64_t decoder_stalls{};
  std::uint32_t in_flight_images{};
  bool eos_submitted{}, eos_drained{};
};

// Authoritative Android strict path: AMediaExtractor -> AMediaCodec ->
// AImageReader PRIVATE surface -> retained AHardwareBuffer.  The class never
// exposes image planes and never has a software/CPU fallback.
class AndroidMediaCodecAhbDecoder final {
public:
  explicit AndroidMediaCodecAhbDecoder(AndroidMediaCodecSessionConfig);
  ~AndroidMediaCodecAhbDecoder();
  AndroidMediaCodecAhbDecoder(const AndroidMediaCodecAhbDecoder&) = delete;
  AndroidMediaCodecAhbDecoder& operator=(const AndroidMediaCodecAhbDecoder&) = delete;

  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult decode_next(NativeMediaSurfacePtr&) noexcept;
  [[nodiscard]] DigitorResult seek(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult flush() noexcept;
  void cancel() noexcept;
  [[nodiscard]] AndroidMediaCodecCapabilities capabilities() const;
  [[nodiscard]] AndroidDecoderStatistics statistics() const;
  [[nodiscard]] const std::string& diagnostic() const noexcept;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace digitor
