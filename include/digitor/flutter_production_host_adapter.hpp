#pragma once

#include "digitor/flutter_production_c_api.h"
#include "digitor/native_preview_presentation.hpp"
#include "digitor/production_hardware_decode.hpp"
#include "digitor/production_media_graph_runtime.hpp"
#include "digitor/production_encoder_factory.hpp"
#if defined(_WIN32)
#include "digitor/windows_unified_export_factory.hpp"
#endif

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
using ProductionPreviewTargetBinder = std::function<DigitorResult(
    std::uint64_t native_target_handle, std::uint32_t width,
    std::uint32_t height, std::int32_t handle_type, std::string& diagnostic)>;
using ProductionTextureDescriptorBuilder = std::function<DigitorResult(
    const ProcessedGpuFramePtr& frame, std::uint64_t generation,
    DigitorNativeGpuTextureDescriptor& descriptor, std::string& diagnostic)>;

struct FlutterProductionHostAdapterInputs {
  ProductionDecoderFactory decoder_factory;
  ProductionTimestampFrameResolver frame_resolver;
  std::shared_ptr<NativePreviewPresentationSession> preview_session;
  HardwareEncoderCallbacks encoder_callbacks;
  ProductionEncoderFactory encoder_factory;
  ProductionTextureDescriptorBuilder texture_descriptor_builder;
  ProductionPreviewTargetBinder preview_target_binder;
  DigitorNativePreviewCapabilities preview_capabilities{};
  EncoderBackend encoder_backend{EncoderBackend::software};
  std::int32_t fps_num{30};
  std::int32_t fps_den{1};
  std::int64_t video_bitrate{12'000'000};
  std::uint64_t required_device_identity{};
  std::uint64_t required_context_identity{};
};

void install_windows_default_export_factory(
    FlutterProductionHostAdapterInputs& inputs) noexcept;

class FlutterProductionHostAdapter final {
 public:
  explicit FlutterProductionHostAdapter(FlutterProductionHostAdapterInputs inputs);
  ~FlutterProductionHostAdapter();

  FlutterProductionHostAdapter(const FlutterProductionHostAdapter&) = delete;
  FlutterProductionHostAdapter& operator=(const FlutterProductionHostAdapter&) = delete;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] DigitorFlutterProductionHost host() noexcept;
  [[nodiscard]] DigitorFlutterProductionHostV2 host_v2() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

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
  DigitorResult unregister() noexcept;

 private:
  std::unique_ptr<FlutterProductionHostAdapter> adapter_;
  void* registered_user_data_{};
  DigitorResult result_{DIGITOR_RESULT_NOT_INITIALIZED};
};

inline RegisteredFlutterProductionHost::RegisteredFlutterProductionHost(
    FlutterProductionHostAdapterInputs inputs) {
  try {
#if defined(_WIN32)
    auto media_source =
        std::make_shared<windows_unified_export_detail::MediaSourcePathState>();
    if (inputs.decoder_factory) {
      auto decoder_factory = std::move(inputs.decoder_factory);
      inputs.decoder_factory =
          [decoder_factory = std::move(decoder_factory), media_source](
              const std::string& media_path,
              std::string& diagnostic) mutable
          -> std::unique_ptr<ProductionHardwareDecodeSession> {
        auto decoder = decoder_factory(media_path, diagnostic);
        if (decoder) media_source->set(media_path);
        return decoder;
      };
    }

    if (!inputs.encoder_factory && inputs.texture_descriptor_builder &&
        inputs.preview_capabilities.backend ==
            DIGITOR_NATIVE_TEXTURE_BACKEND_D3D12 &&
        inputs.preview_capabilities.handle_type ==
            DIGITOR_NATIVE_TEXTURE_HANDLE_DXGI_SHARED_HANDLE &&
        inputs.preview_capabilities.native_gpu_preview_available) {
      inputs.encoder_backend = EncoderBackend::quick_sync;
      inputs.encoder_factory = make_windows_unified_export_factory(
          inputs.texture_descriptor_builder,
          [media_source]() { return media_source->get(); });
    }

    install_windows_default_export_factory(inputs);
#endif
    adapter_ = std::make_unique<FlutterProductionHostAdapter>(std::move(inputs));
    if (!adapter_ || !adapter_->valid()) {
      result_ = DIGITOR_RESULT_NOT_INITIALIZED;
      return;
    }
    auto host = adapter_->host_v2();
    result_ = digitor_flutter_production_register_host_v2(&host);
    if (result_ == DIGITOR_RESULT_OK) registered_user_data_ = host.base.user_data;
  } catch (const std::bad_alloc&) {
    result_ = DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    result_ = DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

inline RegisteredFlutterProductionHost::~RegisteredFlutterProductionHost() {
  (void)unregister();
}

inline DigitorResult RegisteredFlutterProductionHost::unregister() noexcept {
  if (!registered_user_data_) return DIGITOR_RESULT_OK;
  const auto result =
      digitor_flutter_production_unregister_host(registered_user_data_);
  if (result == DIGITOR_RESULT_OK) {
    registered_user_data_ = nullptr;
    result_ = DIGITOR_RESULT_NOT_INITIALIZED;
  }
  return result;
}

}  // namespace digitor
