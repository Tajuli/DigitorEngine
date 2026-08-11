#include "digitor/ffmpeg_d3d11va_surface.hpp"

#include <cassert>

int main() {
  using namespace digitor;
  FfmpegD3D11vaExtractionResult out;
  const auto low_level = extract_ffmpeg_d3d11va_surface(nullptr, out);
  assert(low_level == DIGITOR_RESULT_INVALID_ARGUMENT ||
         low_level == DIGITOR_RESULT_UNSUPPORTED);
  assert(!out.acquire_sync_created);
  assert(!out.no_cpu_transfer);

  out = {};
  constexpr std::int64_t engine_timestamp_us = 123456;
  const auto production = extract_ffmpeg_d3d11va_surface(
      nullptr, engine_timestamp_us, out);
  assert(production == DIGITOR_RESULT_INVALID_ARGUMENT ||
         production == DIGITOR_RESULT_UNSUPPORTED);
  assert(!out.acquire_sync_created);
  assert(!out.no_cpu_transfer);
  return 0;
}
