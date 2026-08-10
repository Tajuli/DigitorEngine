#pragma once

#include "digitor/flutter_production_c_api.h"
#include "digitor/native_preview_presentation.hpp"
#include "digitor/production_hardware_decode.hpp"
#include "digitor/production_media_graph_runtime.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <utility>

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

// Production plugin-owned RAII registration. This turns a complete concrete
// adapter input set into the one process-wide host consumed by Dart's
// digitor_flutter_production_create_registered() path. Destruction unregisters
// only after all production sessions have been released.
class RegisteredFlutterProductionHost final {
 public:
  explicit RegisteredFlutterProductionHost(FlutterProductionHostAdapterInputs inputs);
  ~RegisteredFlutterProductionHost();

  RegisteredFlutterProductionHost(const RegisteredFlutterProductionHost&) = delete;
  RegisteredFlutterProductionHost& operator=(const RegisteredFlutterProductionHost&) = delete;

  [[nodiscard]] bool registered() const noexcept {
    return result_ == DIGITOR_RESULT_OK && registered_user_data_ != nullptr &&
           digitor_flutter_production_host_registered() != 0;
  }
  [[nodiscard]] DigitorResult result() const noexcept { return result_; }

 private:
  std::unique_ptr<FlutterProductionHostAdapter> adapter_;
  void* registered_user_data_{};
  DigitorResult result_{DIGITOR_RESULT_NOT_INITIALIZED};
};

inline RegisteredFlutterProductionHost::RegisteredFlutterProductionHost(
    FlutterProductionHostAdapterInputs inputs) {
  try {
    adapter_ = std::make_unique<FlutterProductionHostAdapter>(std::move(inputs));
    if (!adapter_ || !adapter_->valid()) {
      result_ = DIGITOR_RESULT_NOT_INITIALIZED;
      return;
    }
    auto host = adapter_->host();
    result_ = digitor_flutter_production_register_host(&host);
    if (result_ == DIGITOR_RESULT_OK) registered_user_data_ = host.user_data;
  } catch (const std::bad_alloc&) {
    result_ = DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    result_ = DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

inline RegisteredFlutterProductionHost::~RegisteredFlutterProductionHost() {
  if (!registered_user_data_) return;
  if (digitor_flutter_production_unregister_host(registered_user_data_) ==
      DIGITOR_RESULT_OK) {
    registered_user_data_ = nullptr;
  }
}

}  // namespace digitor
