#include "digitor/timestamp_normalization.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

int main() {
  using namespace digitor;

  assert(compensated_audio_position_us(1'000'000, {40'000, 10'000, 5'000}) == 955'000);

  bool invalid_latency = false;
  try {
    (void)compensated_audio_position_us(0, {-1, 0, 0});
  } catch (const std::invalid_argument&) {
    invalid_latency = true;
  }
  assert(invalid_latency);

  const std::vector<RawVideoTimestamp> input{
      {0, 40'000, 0},
      {80'000, 0, 2},
      {40'000, 0, 1},
      {900'000, 33'333, 3},
      {100'000, 33'333, 4},
  };
  const auto normalized = normalize_vfr_timestamps(input);
  assert(normalized.size() == 5);
  assert(normalized[0].pts_us == 0);
  assert(normalized[1].pts_us == 40'000);
  assert(normalized[1].duration_us == 40'000);
  assert(normalized[1].duration_inferred);
  assert(normalized[2].pts_us == 80'000);
  assert(normalized[2].duration_us == 33'333);
  assert(normalized[3].pts_us == 113'333);
  assert(normalized[3].discontinuity_corrected);
  assert(normalized[4].pts_us == 146'666);
  assert(normalized[4].discontinuity_corrected);

  bool invalid_config = false;
  try {
    (void)normalize_vfr_timestamps({}, {0, 33'333, 500'000});
  } catch (const std::invalid_argument&) {
    invalid_config = true;
  }
  assert(invalid_config);

  bool overflow = false;
  try {
    (void)normalize_vfr_timestamps({{std::numeric_limits<std::int64_t>::max() - 2, 10, 0}});
  } catch (const std::overflow_error&) {
    overflow = true;
  }
  assert(overflow);

  return 0;
}
