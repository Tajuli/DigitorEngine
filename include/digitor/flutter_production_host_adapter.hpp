#pragma once

#include "digitor/flutter_production_c_api.h"
#include "digitor/native_preview_presentation.hpp"
#include "digitor/production_hardware_decode.hpp"
#include "digitor/production_media_graph_runtime.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

using ProductionDecoderFactory = std::function<std::unique_ptr<ProductionHardwareDecodeSession>(
    const std::string& media_path, std::string& diagnostic)>;
using ProductionTimestampFrameResolver = std::function<FrameNumber(std::int64_t timestamp_us)>;
using ProductionTextureDescriptorBuilder = std::function<DigitorResult(
    const ProcessedGpuFramePtr& frame, std::uint64_t generation,
    DigitorNativeGpuTextureDescriptor& descriptor, std::string& diagnostic)>;

struct FlutterProductionHostAdapterInputs {
  ProductionDecoderFactory decoder_factory;
  ProductionTimestampFrameResolver frame_resolver;
  std::shared_ptr<NativePreviewPresentationSession> preview_session;
  HardwareEncoderCallbacks encoder_callbacks;
  ProductionTextureDescriptorBuilder texture_descriptor_builder;
  DigitorNativePreviewCapabilities preview_capabilities{};
  EncoderBackend encoder_backend{EncoderBackend::software};
  std::int32_t fps_num{30};
  std::int32_t fps_den{1};
  std::int64_t video_bitrate{12'000'000};
  std::uint64_t required_device_identity{};
  std::uint64_t required_context_identity{};
};

// Owns the callback state behind DigitorFlutterProductionHost. Flutter/Dart only
// drives the stable production-session C ABI; decode, graph execution, preview
// processing, hardware export and cancellation stay inside DigitorEngine.
class FlutterProductionHostAdapter final {
 public:
  explicit FlutterProductionHostAdapter(FlutterProductionHostAdapterInputs inputs);
  ~FlutterProductionHostAdapter();

  FlutterProductionHostAdapter(const FlutterProductionHostAdapter&) = delete;
  FlutterProductionHostAdapter& operator=(const FlutterProductionHostAdapter&) = delete;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] DigitorFlutterProductionHost host() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace digitor
