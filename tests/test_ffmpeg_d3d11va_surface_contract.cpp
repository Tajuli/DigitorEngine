#include "digitor/ffmpeg_d3d11va_surface.hpp"

#include <cassert>

int main() {
  using namespace digitor;
  FfmpegD3D11vaExtractionResult out;
  const auto result = extract_ffmpeg_d3d11va_surface(nullptr, 123456, out);
  assert(result == DIGITOR_RESULT_INVALID_ARGUMENT ||
         result == DIGITOR_RESULT_UNSUPPORTED);
  assert(!out.no_cpu_transfer);
  return 0;
}
