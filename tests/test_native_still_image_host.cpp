#include "digitor/native_still_image_host.hpp"

#include <cassert>
#include <string>

int main() {
  using namespace digitor;

  assert(native_still_backend_matches_platform(
      NativeStillPlatform::windows, DIGITOR_RENDERER_D3D12));
  assert(native_still_backend_matches_platform(
      NativeStillPlatform::android, DIGITOR_RENDERER_OPENGL_ES));
  assert(native_still_backend_matches_platform(
      NativeStillPlatform::apple, DIGITOR_RENDERER_METAL));
  assert(!native_still_backend_matches_platform(
      NativeStillPlatform::windows, DIGITOR_RENDERER_METAL));

  NativeStillImageLimits limits;
  limits.max_dimension = 8192;
  limits.max_decoded_bytes = 8192ULL * 8192ULL * 4ULL;
  limits.tile_width = 2048;
  limits.tile_height = 1024;
  const auto tiles = make_native_still_tile_plan(4097, 2050, limits);
  assert(tiles.size() == 9);
  assert(tiles.front().x == 0 && tiles.front().y == 0);
  assert(tiles.back().x == 4096 && tiles.back().y == 2048);
  assert(tiles.back().width == 1 && tiles.back().height == 2);
  assert(tiles.back().index == 8 && tiles.back().count == 9);

  NativeStillImageInfo info;
  info.encoded_width = 3000;
  info.encoded_height = 4000;
  info.display_width = 4000;
  info.display_height = 3000;
  info.orientation = ImageOrientation::rotate_90;
  info.orientation_applied = true;
  info.encoded_format = NativeStillEncodedFormat::jpeg;
  info.bits_per_channel = 8;
  info.color_metadata_identity = "icc:test";
  assert(native_still_image_info_valid(info, limits));
  info.display_width = 9000;
  assert(!native_still_image_info_valid(info, limits));

  NativeStillImageHostConfig invalid;
  invalid.platform = NativeStillPlatform::windows;
  invalid.backend = DIGITOR_RENDERER_METAL;
  auto result = make_native_still_image_host(invalid);
  assert(!result && result.result == DIGITOR_RESULT_INVALID_ARGUMENT);

  NativeStillImageHostConfig config;
  config.platform = NativeStillPlatform::windows;
  config.backend = DIGITOR_RENDERER_D3D12;
  config.context_identity = reinterpret_cast<const void*>(0x1);
  config.device_identity = "test-d3d12-device";
  config.limits = limits;

  NativeStillImageServicePack pack;
  pack.platform = NativeStillPlatform::windows;
  pack.backend = DIGITOR_RENDERER_D3D12;
  pack.codec_identity = "WIC-test-adapter";
  pack.applies_orientation = true;
  pack.preserves_color_metadata = true;
  pack.supports_alpha = true;
  pack.services.decode_to_gpu =
      [](const std::string&, NativeStillImageInfo&, ProcessedGpuFramePtr&,
         std::string&) { return DIGITOR_RESULT_BACKEND_UNAVAILABLE; };
  pack.services.resize_gpu =
      [](const ProcessedGpuFramePtr&, std::uint32_t, std::uint32_t,
         std::int64_t, ProcessedGpuFramePtr&, std::string&) {
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      };
  pack.services.process_graph =
      [](const GpuImageSessionProcessRequest&, ProcessedGpuFramePtr&,
         std::string&) { return DIGITOR_RESULT_BACKEND_UNAVAILABLE; };
  pack.services.encode_from_gpu =
      [](const ProcessedGpuFramePtr&, const std::string&,
         const ImageExportOptions&, const NativeStillImageInfo&,
         const NativeStillImageProgress&) {
        return ImageIoResult{DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                             "test encoder unavailable"};
      };

  std::string diagnostic;
  assert(bind_native_still_image_service_pack(config, std::move(pack),
                                               diagnostic) ==
         DIGITOR_RESULT_OK);
  assert(config.codec_identity == "WIC-test-adapter");

  result = make_native_still_image_host(std::move(config));
  assert(result);
  assert(result.host.image_io.backend == DIGITOR_RENDERER_D3D12);
  assert(result.host.image_io.context_identity != nullptr);
  assert(gpu_image_session_host_valid(result.host));

  return 0;
}
