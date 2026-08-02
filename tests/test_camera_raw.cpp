#include "digitor/camera_raw.hpp"

#include <cassert>
#include <memory>
#include <string>

using namespace digitor;

namespace {

RawAdapterCallbacks make_adapter(bool gpu) {
  RawAdapterCallbacks adapter;
  adapter.capabilities.name = gpu ? "Mock GPU RAW SDK" : "Mock CPU RAW SDK";
  adapter.capabilities.version = "1.0";
  adapter.capabilities.codecs = {RawCodec::cinema_dng, RawCodec::generic_vendor_raw};
  adapter.capabilities.gpu_decode = gpu;
  adapter.capabilities.cpu_decode = !gpu;
  adapter.capabilities.random_access = true;
  adapter.capabilities.metadata_only_probe = true;
  adapter.capabilities.debayer_quality_control = true;
  adapter.capabilities.white_balance_control = true;
  adapter.capabilities.iso_control = true;
  adapter.capabilities.exposure_control = true;
  adapter.capabilities.highlight_recovery = true;

  adapter.probe = [](const std::string& path, RawClipMetadata& metadata,
                     std::string& error) {
    if (path.find(".dng") == std::string::npos) {
      error = "unsupported extension";
      return DIGITOR_RESULT_UNSUPPORTED;
    }
    metadata.codec = RawCodec::cinema_dng;
    metadata.vendor = "Digitor Test Camera";
    metadata.camera_model = "RAW-1";
    metadata.width = 4;
    metadata.height = 2;
    metadata.bit_depth = 14;
    metadata.frame_count = 8;
    metadata.frame_rate = 24.0;
    metadata.duration_seconds = 8.0 / 24.0;
    metadata.camera_iso = 800.0;
    metadata.camera_temperature_kelvin = 5600.0;
    metadata.native_gamut = "Camera Wide Gamut";
    metadata.native_transfer = "Camera Log";
    metadata.supports_gpu_decode = true;
    metadata.supports_highlight_recovery = true;
    return DIGITOR_RESULT_OK;
  };
  adapter.open = [](const std::string&, std::shared_ptr<void>& session,
                    std::string&) {
    session = std::make_shared<int>(7);
    return DIGITOR_RESULT_OK;
  };
  adapter.decode = [gpu](const std::shared_ptr<void>& session,
                         const RawDecodeRequest& request,
                         RawDecodedFrame& frame, std::string& error) {
    if (!session) {
      error = "missing session";
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }
    frame.width = 4;
    frame.height = 2;
    frame.frame_index = request.frame_index;
    frame.timestamp_seconds = request.timestamp_seconds;
    frame.output_gamut = "ACEScg";
    frame.output_transfer = "Linear";
    if (gpu) {
      frame.backend = DIGITOR_RENDERER_D3D12;
      frame.format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
      frame.native_surface = std::make_shared<int>(42);
      frame.gpu_resident = true;
    } else {
      frame.backend = DIGITOR_RENDERER_CPU;
      frame.format = DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT;
      frame.rgba32f.assign(4u * 2u * 4u, 0.5f);
      frame.gpu_resident = false;
    }
    return DIGITOR_RESULT_OK;
  };
  adapter.seek = [](const std::shared_ptr<void>& session, std::uint64_t,
                    std::string& error) {
    if (!session) {
      error = "missing session";
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }
    return DIGITOR_RESULT_OK;
  };
  adapter.close = [](std::shared_ptr<void>& session) { session.reset(); };
  return adapter;
}

}  // namespace

int main() {
  CameraRawEngine engine(2);
  std::string diagnostic;

  assert(engine.register_adapter(make_adapter(true), &diagnostic) == DIGITOR_RESULT_OK);
  assert(engine.register_adapter(make_adapter(true), &diagnostic) ==
         DIGITOR_RESULT_RESOURCE_IN_USE);
  assert(engine.adapters().size() == 1);

  RawClipMetadata metadata;
  assert(engine.probe("shot.dng", metadata, &diagnostic) == DIGITOR_RESULT_OK);
  assert(metadata.codec == RawCodec::cinema_dng);
  assert(metadata.bit_depth == 14);
  assert(engine.probe("shot.mp4", metadata, &diagnostic) == DIGITOR_RESULT_UNSUPPORTED);

  RawDecodeSettings invalid;
  invalid.temperature_kelvin = 100.0;
  assert(CameraRawEngine::validate_settings(invalid, &diagnostic) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);

  assert(engine.open("shot.dng", &diagnostic) == DIGITOR_RESULT_OK);
  assert(engine.is_open());
  assert(engine.metadata().frame_count == 8);

  RawDecodeRequest request;
  request.source_path = "shot.dng";
  request.frame_index = 2;
  request.timestamp_seconds = 2.0 / 24.0;
  request.settings.require_gpu = true;
  request.settings.output_encoding = RawOutputEncoding::scene_linear;

  RawDecodedFrame first;
  assert(engine.decode(request, first, &diagnostic) == DIGITOR_RESULT_OK);
  assert(first.gpu_resident);
  assert(first.native_surface);
  assert(first.backend == DIGITOR_RENDERER_D3D12);
  assert(first.output_gamut == "ACEScg");

  RawDecodedFrame cached;
  assert(engine.decode(request, cached, &diagnostic) == DIGITOR_RESULT_OK);
  assert(cached.native_surface == first.native_surface);
  assert(engine.telemetry().cache_hits == 1);

  assert(engine.seek(4, &diagnostic) == DIGITOR_RESULT_OK);
  assert(engine.telemetry().decoded_frames == 1);
  assert(engine.telemetry().gpu_frames == 1);

  request.frame_index = 99;
  assert(engine.decode(request, cached, &diagnostic) == DIGITOR_RESULT_INVALID_ARGUMENT);

  engine.close();
  assert(!engine.is_open());
  assert(engine.decode(request, cached, &diagnostic) == DIGITOR_RESULT_NOT_INITIALIZED);

  CameraRawEngine cpu_engine;
  assert(cpu_engine.register_adapter(make_adapter(false), &diagnostic) == DIGITOR_RESULT_OK);
  assert(cpu_engine.open("shot.dng", &diagnostic) == DIGITOR_RESULT_OK);
  request.frame_index = 0;
  request.settings.require_gpu = true;
  assert(cpu_engine.decode(request, cached, &diagnostic) ==
         DIGITOR_RESULT_BACKEND_UNAVAILABLE);
  request.settings.require_gpu = false;
  assert(cpu_engine.decode(request, cached, &diagnostic) == DIGITOR_RESULT_OK);
  assert(!cached.gpu_resident);
  assert(cached.rgba32f.size() == 32);

  return 0;
}
