#include "digitor/ffmpeg_d3d11va_zero_copy_decoder.hpp"

#include <cassert>
#include <stdexcept>

int main() {
  using namespace digitor;

  FfmpegD3D11vaZeroCopyOptions strict{};
  assert(strict.fallback == ZeroCopyFallbackPolicy::forbid_cpu_transfer);
  assert(strict.require_p010_for_10bit);

  FfmpegD3D11vaZeroCopyResult result{};
  assert(!result.zero_copy_attempted);
  assert(!result.zero_copy_succeeded);
  assert(!result.legacy_fallback_allowed);
  assert(!result.legacy_fallback_requested);

#if !defined(_WIN32)
  bool rejected = false;
  try {
    FfmpegD3D11vaZeroCopyDecoder decoder(nullptr, strict);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  assert(rejected);
#endif

  return 0;
}
