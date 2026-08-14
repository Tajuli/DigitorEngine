#include "digitor/ffmpeg_d3d11va_surface.hpp"

#if defined(min) || defined(max)
#error "Digitor public headers must not define Windows min/max macros"
#endif

#include <algorithm>
#include <cstddef>
#include <limits>

int main() {
  const auto greatest = std::max(1, 2);
  const auto size_max = std::numeric_limits<std::size_t>::max();
  const auto int_max = std::numeric_limits<int>::max();
  return greatest == 2 && size_max > 0 && int_max > 0 ? 0 : 1;
}
