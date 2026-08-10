#pragma once

#include "digitor/android_hardware_encode_adapter.hpp"
#include "digitor/apple_hardware_encode_adapter.hpp"
#include "digitor/native_preview_presentation.hpp"
#include "digitor/production_timeline_gpu_binding.hpp"
#include "digitor/production_encoder_factory.hpp"
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
  // Optional legacy eager snapshot. Production Flutter attachment no longer
  // requires this value; when omitted, encoder adapters are created lazily at
  // export start from the V2 frozen export snapshot.
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

  // Export is intentionally lazy. The factory binds a frozen export snapshot
  // only when export starts, so plugin attachment and preview never depend on
  // an export path, codec selection, output path, or encoder-open operation.
  ProductionEncoderFactory encoder_factory;

  // Compatibility surface for callers that still provide an eager snapshot.
  // New production Flutter code uses encoder_factory instead.
  HardwareEncoderCallbacks encoder_callbacks;
  std::function<bool()> encoder_zero_copy_qualified;
  bool windows_vulkan_interop_qualified{};
  std::string diagnostic;

  [[nodiscard]] explicit operator bool() const noexcept {
    return preview_host && preview_host->valid() && preview_session &&
           timeline_binding && timeline_binding->valid() &&
           static_cast<bool>(encoder_factory) && diagnostic.empty();
  }
};

[[nodiscard]] inline ProductionEncoderFactoryResult create_production_encoder(
    ProductionPlatform platform,
    std::shared_ptr<const ExportRenderSnapshot> snapshot,
    WindowsHardwareEncoderHost windows,
    AndroidHardwareEncoderHost android,
    AppleHardwareEncoderHost apple,
    WindowsVulkanZeroCopyInterop windows_vulkan) {
  ProductionEncoderFactoryResult out{};
  if (!snapshot) {
    out.diagnostic = "immutable export snapshot is required at export start";
    return out;
  }
  const auto validation = validate_export_snapshot(*snapshot);
  if (!validation) {
    out.diagnostic = validation.diagnostic;
    return out;
  }

  const auto renderer = snapshot->renderer_backend();
  switch (platform) {
    case ProductionPlatform::windows: {
      if (renderer != DIGITOR_RENDERER_D3D12 &&
          renderer != DIGITOR_RENDERER_VULKAN) {
        out.diagnostic = "Windows production export requires D3D12 or Vulkan";
        return out;
      }
      if (renderer == DIGITOR_RENDERER_VULKAN) {
        out.windows_vulkan_interop_qualified =
            validate_windows_vulkan_zero_copy_interop(windows_vulkan);
        if (!out.windows_vulkan_interop_qualified) {
          out.diagnostic =
              "Windows Vulkan external-memory/semaphore interop unavailable";
          return out;
        }
      }
      auto adapter = create_windows_hardware_encode_adapter(
          std::move(snapshot), std::move(windows));
      out.callbacks = std::move(adapter.callbacks);
      out.zero_copy_qualified =
          [qualification = std::move(adapter.qualification)] {
            const auto q = qualification();
            return q.adapter_opened && q.gpu_frame_submitted &&
                   q.synchronization_waited && q.native_resource_registered &&
                   q.bitstream_produced && q.atomic_output_finalized &&
                   q.no_cpu_readback && q.no_cpu_staging;
          };
      break;
    }
    case ProductionPlatform::android: {
      if (renderer != DIGITOR_RENDERER_VULKAN &&
          renderer != DIGITOR_RENDERER_OPENGL_ES) {
        out.diagnostic = "Android production export requires Vulkan or GLES";
        return out;
      }
      auto adapter = create_android_hardware_encode_adapter(
          std::move(snapshot), std::move(android));
      out.callbacks = std::move(adapter.callbacks);
      out.zero_copy_qualified =
          [qualification = std::move(adapter.qualification)] {
            const auto q = qualification();
            return q.codec_opened && q.input_surface_created &&
                   q.gpu_frame_submitted && q.acquire_sync_waited &&
                   q.release_sync_published &&
                   q.ahardwarebuffer_or_surface_bound &&
                   q.bitstream_produced && q.mp4_finalized &&
                   q.no_cpu_readback && q.no_cpu_staging;
          };
      break;
    }
    case ProductionPlatform::macos:
    case ProductionPlatform::ios: {
      if (renderer != DIGITOR_RENDERER_METAL) {
        out.diagnostic = "Apple production export requires Metal";
        return out;
      }
      auto adapter = create_apple_hardware_encode_adapter(
          std::move(snapshot), std::move(apple));
      out.callbacks = std::move(adapter.callbacks);
      out.zero_copy_qualified =
          [qualification = std::move(adapter.qualification)] {
            const auto q = qualification();
            return q.adapter_opened && q.metal_completion_waited &&
                   q.iosurface_pixel_buffer_acquired &&
                   q.attachments_propagated && q.bitstream_produced &&
                   q.atomic_output_finalized && q.no_cpu_readback &&
                   q.no_cpu_staging;
          };
      break;
    }
  }

  if (!hardware_encoder_callbacks_complete(out.callbacks) ||
      !out.zero_copy_qualified) {
    out.callbacks = {};
    out.zero_copy_qualified = {};
    out.diagnostic = "native hardware encoder bindings are incomplete";
    return out;
  }
  return out;
}

[[nodiscard]] inline ProductionPlatformAssembly create_production_platform_assembly(
    ProductionPlatformFactoryInputs inputs) {
  ProductionPlatformAssembly out{};
  out.platform = inputs.platform;

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

  const auto platform = inputs.platform;
  auto windows = std::move(inputs.encoder.windows);
  auto android = std::move(inputs.encoder.android);
  auto apple = std::move(inputs.encoder.apple);
  auto windows_vulkan = std::move(inputs.windows_vulkan);
  out.encoder_factory =
      [platform, windows, android, apple, windows_vulkan](
          std::shared_ptr<const ExportRenderSnapshot> snapshot) mutable {
        return create_production_encoder(
            platform, std::move(snapshot), windows, android, apple,
            windows_vulkan);
      };

  // Preserve the previous eager behavior only when an explicit snapshot was
  // supplied. Omitting the snapshot is now the normal preview-attachment path.
  if (inputs.encoder.snapshot) {
    auto eager = out.encoder_factory(inputs.encoder.snapshot);
    if (!eager) {
      out.diagnostic = eager.diagnostic.empty()
                           ? "failed to prepare eager production encoder"
                           : std::move(eager.diagnostic);
      return out;
    }
    out.encoder_callbacks = std::move(eager.callbacks);
    out.encoder_zero_copy_qualified = std::move(eager.zero_copy_qualified);
    out.windows_vulkan_interop_qualified =
        eager.windows_vulkan_interop_qualified;
  }

  return out;
}

}  // namespace digitor
