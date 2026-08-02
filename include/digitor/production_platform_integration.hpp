#pragma once

#include "digitor/android_hardware_encode_adapter.hpp"
#include "digitor/apple_hardware_encode_adapter.hpp"
#include "digitor/native_preview_presentation.hpp"
#include "digitor/production_timeline_gpu_binding.hpp"
#include "digitor/windows_hardware_encode_adapter.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace digitor {

enum class ProductionPlatform : std::uint32_t {
  windows = 1,
  android = 2,
  macos = 3,
  ios = 4,
};

struct FlutterNativeTextureRegistrar final {
  ProductionPlatform platform{ProductionPlatform::windows};
  DigitorRendererBackend backend{DIGITOR_RENDERER_CPU};
  const void* device_identity{};
  std::string device_name;
  std::function<bool()> attached;
  // The platform embedding owns registration/replacement and must retain the
  // exact frame until Flutter reports that the generation was consumed.
  std::function<DigitorResult(const ProcessedGpuFramePtr&, std::uint64_t)> register_or_present;
};

class ConcreteFlutterTextureHost final : public NativePreviewTextureHost {
 public:
  explicit ConcreteFlutterTextureHost(FlutterNativeTextureRegistrar registrar)
      : registrar_(std::move(registrar)) {}

  [[nodiscard]] bool valid() const noexcept {
    return registrar_.backend != DIGITOR_RENDERER_CPU &&
           registrar_.device_identity != nullptr && !registrar_.device_name.empty() &&
           static_cast<bool>(registrar_.attached) &&
           static_cast<bool>(registrar_.register_or_present) &&
           platform_backend_valid(registrar_.platform, registrar_.backend);
  }

  [[nodiscard]] bool attached() const noexcept override {
    return valid() && registrar_.attached();
  }

  [[nodiscard]] DigitorRendererBackend backend() const noexcept override {
    return registrar_.backend;
  }

  [[nodiscard]] const void* device_identity() const noexcept override {
    return registrar_.device_identity;
  }

  DigitorResult present(const ProcessedGpuFramePtr& frame,
                        std::uint64_t generation) noexcept override {
    if (!valid() || !attached()) return DIGITOR_RESULT_NOT_INITIALIZED;
    if (!frame || frame->backend() != registrar_.backend || !frame->ready() ||
        !frame->context_live() ||
        !frame->has_context_identity(registrar_.device_identity) || generation == 0) {
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    const auto format = frame->metadata().format;
    if (format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT &&
        format != DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT) {
      return DIGITOR_RESULT_UNSUPPORTED;
    }
    return registrar_.register_or_present(frame, generation);
  }

  [[nodiscard]] ProductionPlatform platform() const noexcept {
    return registrar_.platform;
  }

 private:
  static bool platform_backend_valid(ProductionPlatform platform,
                                     DigitorRendererBackend backend) noexcept {
    switch (platform) {
      case ProductionPlatform::windows:
        return backend == DIGITOR_RENDERER_VULKAN ||
               backend == DIGITOR_RENDERER_D3D12;
      case ProductionPlatform::android:
        return backend == DIGITOR_RENDERER_VULKAN ||
               backend == DIGITOR_RENDERER_OPENGL_ES;
      case ProductionPlatform::macos:
      case ProductionPlatform::ios:
        return backend == DIGITOR_RENDERER_METAL;
    }
    return false;
  }

  FlutterNativeTextureRegistrar registrar_;
};

struct WindowsVulkanZeroCopyInterop final {
  bool available{};
  bool dxgi_external_memory{};
  bool external_semaphore{};
  bool nv12{};
  bool p010{};
  bool adapter_identity_matched{};
  bool no_cpu_readback{true};
  bool no_cpu_staging{true};
  std::string adapter_identity;
  std::function<DigitorResult(const ProcessedGpuFramePtr&, ProcessedGpuFramePtr&,
                              std::string&)> export_to_d3d12_encoder_resource;
};

[[nodiscard]] inline bool validate_windows_vulkan_zero_copy_interop(
    const WindowsVulkanZeroCopyInterop& interop) noexcept {
  return interop.available && interop.dxgi_external_memory &&
         interop.external_semaphore && interop.nv12 && interop.p010 &&
         interop.adapter_identity_matched && interop.no_cpu_readback &&
         interop.no_cpu_staging && !interop.adapter_identity.empty() &&
         static_cast<bool>(interop.export_to_d3d12_encoder_resource);
}

struct ProductionEncoderFactoryInputs final {
  std::shared_ptr<const ExportRenderSnapshot> snapshot;
  WindowsHardwareEncoderHost windows;
  AndroidHardwareEncoderHost android;
  AppleHardwareEncoderHost apple;
};

struct ProductionPlatformFactoryInputs final {
  ProductionPlatform platform{ProductionPlatform::windows};
  ProductionTimelineGpuHost timeline;
  FlutterNativeTextureRegistrar flutter;
  ProductionEncoderFactoryInputs encoder;
  WindowsVulkanZeroCopyInterop windows_vulkan;
};

struct ProductionPlatformAssembly final {
  ProductionPlatform platform{ProductionPlatform::windows};
  std::shared_ptr<ConcreteFlutterTextureHost> preview_host;
  std::shared_ptr<NativePreviewPresentationSession> preview_session;
  std::shared_ptr<ProductionTimelineGpuBinding> timeline_binding;
  HardwareEncoderCallbacks encoder_callbacks;
  std::function<bool()> encoder_zero_copy_qualified;
  bool windows_vulkan_interop_qualified{};
  std::string diagnostic;

  [[nodiscard]] explicit operator bool() const noexcept {
    return preview_host && preview_host->valid() && preview_session &&
           timeline_binding && timeline_binding->valid() &&
           static_cast<bool>(encoder_callbacks.open) &&
           static_cast<bool>(encoder_callbacks.submit_gpu_frame) &&
           static_cast<bool>(encoder_callbacks.drain) &&
           static_cast<bool>(encoder_callbacks.finalize_atomic) &&
           static_cast<bool>(encoder_callbacks.cancel) &&
           static_cast<bool>(encoder_zero_copy_qualified) && diagnostic.empty();
  }
};

[[nodiscard]] inline ProductionPlatformAssembly create_production_platform_assembly(
    ProductionPlatformFactoryInputs inputs) {
  ProductionPlatformAssembly out{};
  out.platform = inputs.platform;

  if (!inputs.encoder.snapshot) {
    out.diagnostic = "immutable export snapshot is required";
    return out;
  }
  if (inputs.timeline.backend != inputs.flutter.backend ||
      inputs.timeline.context_identity != inputs.flutter.device_identity) {
    out.diagnostic = "timeline and Flutter texture host must share backend/device";
    return out;
  }

  out.timeline_binding =
      std::make_shared<ProductionTimelineGpuBinding>(std::move(inputs.timeline));
  out.preview_host =
      std::make_shared<ConcreteFlutterTextureHost>(std::move(inputs.flutter));
  if (!out.timeline_binding->valid() || !out.preview_host->valid()) {
    out.diagnostic = "timeline or Flutter native texture factory is incomplete";
    return out;
  }
  out.preview_session =
      std::make_shared<NativePreviewPresentationSession>(out.preview_host);

  const auto renderer = inputs.encoder.snapshot->renderer_backend();
  switch (inputs.platform) {
    case ProductionPlatform::windows: {
      if (renderer != DIGITOR_RENDERER_D3D12 &&
          renderer != DIGITOR_RENDERER_VULKAN) {
        out.diagnostic = "Windows production assembly requires D3D12 or Vulkan";
        return out;
      }
      if (renderer == DIGITOR_RENDERER_VULKAN) {
        out.windows_vulkan_interop_qualified =
            validate_windows_vulkan_zero_copy_interop(inputs.windows_vulkan);
        if (!out.windows_vulkan_interop_qualified) {
          out.diagnostic = "Windows Vulkan external-memory/semaphore interop unavailable";
          return out;
        }
      }
      auto adapter = create_windows_hardware_encode_adapter(
          inputs.encoder.snapshot, std::move(inputs.encoder.windows));
      out.encoder_callbacks = std::move(adapter.callbacks);
      out.encoder_zero_copy_qualified = [qualification = std::move(adapter.qualification)] {
        const auto q = qualification();
        return q.adapter_opened && q.gpu_frame_submitted &&
               q.synchronization_waited && q.native_resource_registered &&
               q.no_cpu_readback && q.no_cpu_staging;
      };
      break;
    }
    case ProductionPlatform::android: {
      if (renderer != DIGITOR_RENDERER_VULKAN &&
          renderer != DIGITOR_RENDERER_OPENGL_ES) {
        out.diagnostic = "Android production assembly requires Vulkan or GLES";
        return out;
      }
      auto adapter = create_android_hardware_encode_adapter(
          inputs.encoder.snapshot, std::move(inputs.encoder.android));
      out.encoder_callbacks = std::move(adapter.callbacks);
      out.encoder_zero_copy_qualified = [qualification = std::move(adapter.qualification)] {
        const auto q = qualification();
        return q.codec_opened && q.input_surface_created &&
               q.gpu_frame_submitted && q.acquire_sync_waited &&
               q.release_sync_published &&
               q.ahardwarebuffer_or_surface_bound && q.no_cpu_readback &&
               q.no_cpu_staging;
      };
      break;
    }
    case ProductionPlatform::macos:
    case ProductionPlatform::ios: {
      if (renderer != DIGITOR_RENDERER_METAL) {
        out.diagnostic = "Apple production assembly requires Metal";
        return out;
      }
      auto adapter = create_apple_hardware_encode_adapter(
          inputs.encoder.snapshot, std::move(inputs.encoder.apple));
      out.encoder_callbacks = std::move(adapter.callbacks);
      out.encoder_zero_copy_qualified = [qualification = std::move(adapter.qualification)] {
        const auto q = qualification();
        return q.adapter_opened && q.metal_completion_waited &&
               q.iosurface_pixel_buffer_acquired &&
               q.attachments_propagated && q.no_cpu_readback &&
               q.no_cpu_staging;
      };
      break;
    }
  }

  return out;
}

}  // namespace digitor
