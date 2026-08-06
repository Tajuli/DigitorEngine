#include "digitor/native_image_runtime.hpp"

#include <cassert>

int main() {
  using namespace digitor;

  const auto tiles = NativeImageRuntime::tiles(1025, 769, 512, 512);
  assert(tiles.size() == 6);
  assert(tiles.front().x == 0 && tiles.front().y == 0);
  assert(tiles.back().x == 1024 && tiles.back().y == 512);
  assert(tiles.back().width == 1 && tiles.back().height == 257);

  assert(NativeImageRuntime::tiles(0, 10, 4, 4).empty());
  assert(NativeImageRuntime::tiles(10, 10, 0, 4).empty());

  const auto services = default_native_image_codec_services();
  assert(!services.implementation_name.empty());
  assert(static_cast<bool>(services.probe));
  assert(static_cast<bool>(services.decode));
  assert(static_cast<bool>(services.encode));

  NativeImageCancellation cancellation;
  assert(!cancellation.cancelled());
  cancellation.cancel();
  assert(cancellation.cancelled());

  NativeImageMetadata metadata;
  metadata.width = 100;
  metadata.height = 50;
  metadata.orientation = NativeImageOrientation::rotate_90;
  metadata.has_alpha = true;
  assert(metadata.width == 100 && metadata.height == 50);

  return 0;
}
