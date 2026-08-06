#include "digitor/native_image_runtime.hpp"

#include <cassert>
#include <stdexcept>

int main() {
  using namespace digitor;
  NativeImageCodecCapabilities caps{true,true,true,true,true,true,true,true,true,true,true};
  assert(native_image_codec_supported(caps, NativeImageCodec::jpeg, false));
  assert(native_image_codec_supported(caps, NativeImageCodec::webp, true));

  NativeStillImageLimits limits;
  limits.max_dimension = 32768;
  limits.max_decoded_bytes = 1024ULL * 1024ULL * 1024ULL;
  limits.tile_width = 2048;
  limits.tile_height = 2048;
  const auto tiles = plan_native_image_tiles(5000, 3000, limits);
  assert(tiles.size() == 6);
  assert(tiles.front().x == 0 && tiles.front().y == 0);
  assert(tiles.back().width == 904 && tiles.back().height == 952);

  bool rejected = false;
  try { (void)plan_native_image_tiles(40000, 1, limits); }
  catch (const std::invalid_argument&) { rejected = true; }
  assert(rejected);

  NativeImageRuntimeConfig config;
  config.host.platform = NativeStillPlatform::windows;
  config.host.backend = DIGITOR_RENDERER_D3D12;
  config.host.context_identity = reinterpret_cast<const void*>(0x1);
  config.host.device_identity = "qualification-device";
  config.platform_services.platform = NativeStillPlatform::windows;
  config.platform_services.capabilities = caps;
  config.platform_services.inspect_metadata = [](const std::string&, NativeStillImageInfo&) {
    return ImageIoResult{};
  };
  auto unavailable = [](auto&&...) { return DIGITOR_RESULT_BACKEND_UNAVAILABLE; };
  config.platform_services.gpu_services.decode_to_gpu = unavailable;
  config.platform_services.gpu_services.resize_gpu = unavailable;
  config.platform_services.gpu_services.process_graph = unavailable;
  config.platform_services.gpu_services.encode_from_gpu =
      [](const ProcessedGpuFramePtr&, const std::string&, const ImageExportOptions&,
         const NativeStillImageInfo&, const NativeStillImageProgress&) {
        return ImageIoResult{DIGITOR_RESULT_BACKEND_UNAVAILABLE, "fixture"};
      };
  const auto runtime = make_native_image_runtime(std::move(config));
  assert(runtime);
  assert(gpu_image_session_host_valid(runtime.host.host));

  NativeImageRuntimeConfig incomplete;
  incomplete.host.platform = NativeStillPlatform::windows;
  incomplete.platform_services.platform = NativeStillPlatform::windows;
  incomplete.platform_services.capabilities = caps;
  incomplete.platform_services.capabilities.encode_webp = false;
  const auto failed = make_native_image_runtime(std::move(incomplete));
  assert(!failed && failed.result == DIGITOR_RESULT_BACKEND_UNAVAILABLE);

  assert(flutter_native_image_descriptor({}).texture_identity == 0);
  return 0;
}
