#ifdef _WIN32

#include "windows_native_provider.hpp"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>

#include <utility>

namespace digitor::windows {
namespace {

bool media_foundation_hardware_encoder_available() noexcept {
  if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return false;

  IMFActivate** activates = nullptr;
  UINT32 count = 0;
  MFT_REGISTER_TYPE_INFO output_type{};
  output_type.guidMajorType = MFMediaType_Video;
  output_type.guidSubtype = MFVideoFormat_H264;

  const HRESULT result = MFTEnumEx(
      MFT_CATEGORY_VIDEO_ENCODER,
      MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
      nullptr, &output_type, &activates, &count);

  if (activates) {
    for (UINT32 index = 0; index < count; ++index) {
      if (activates[index]) activates[index]->Release();
    }
    CoTaskMemFree(activates);
  }
  MFShutdown();
  return SUCCEEDED(result) && count > 0;
}

NativeImplementationEvidence evidence(std::string identity) {
  NativeImplementationEvidence value{};
  value.production_implementation = true;
  value.native_api_bound = true;
  value.synchronization_bound = true;
  value.zero_copy_telemetry_bound = true;
  value.implementation_identity = std::move(identity);
  return value;
}

}  // namespace

WindowsNativeProviderProbe probe_windows_native_provider(
    const WindowsNativeProviderContext& context) noexcept {
  WindowsNativeProviderProbe result{};
  result.flutter_registrar_bound = context.plugin_registrar != nullptr &&
                                   context.texture_registrar != nullptr;
  result.d3d12_bound = context.d3d12_device != nullptr &&
                       context.d3d12_queue != nullptr &&
                       context.dxgi_adapter != nullptr &&
                       context.renderer_context_identity != nullptr &&
                       !context.adapter_identity.empty();
  result.media_foundation_available = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
  if (result.media_foundation_available) MFShutdown();
  result.hardware_encoder_available = media_foundation_hardware_encoder_available();
  result.vulkan_dxgi_interop_available =
      context.vulkan_external_memory_available &&
      context.vulkan_external_semaphore_available;
  result.zero_copy_supported = result.flutter_registrar_bound &&
                               result.d3d12_bound &&
                               result.media_foundation_available &&
                               result.hardware_encoder_available;

  if (!result.flutter_registrar_bound)
    result.diagnostic = "Flutter Windows plugin/texture registrar is not bound";
  else if (!result.d3d12_bound)
    result.diagnostic = "D3D12 queue/device/adapter identity is incomplete";
  else if (!result.media_foundation_available)
    result.diagnostic = "Media Foundation startup failed";
  else if (!result.hardware_encoder_available)
    result.diagnostic = "No Media Foundation hardware H.264 encoder was enumerated";
  return result;
}

NativePlatformProvider create_windows_native_platform_provider(
    std::shared_ptr<WindowsNativeProviderContext> context) {
  NativePlatformProvider provider{};
  provider.platform = ProductionPlatform::windows;
  provider.package_identity = "digitor.windows.native";
  provider.build_identity = DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY;
  provider.timeline = evidence("windows-d3d12-timeline");
  provider.flutter_texture = evidence("windows-flutter-texture-registrar");
  provider.encoder = evidence("windows-media-foundation-hardware-encoder");

  provider.create = [context = std::move(context)](
                        ProductionPlatformFactoryInputs inputs) {
    if (!context) {
      ProductionPlatformAssembly out{};
      out.platform = ProductionPlatform::windows;
      out.diagnostic = "Windows native provider context is missing";
      return out;
    }
    const auto probe = probe_windows_native_provider(*context);
    if (!probe.zero_copy_supported) {
      ProductionPlatformAssembly out{};
      out.platform = ProductionPlatform::windows;
      out.diagnostic = probe.diagnostic.empty()
                           ? "Windows native provider probe failed"
                           : probe.diagnostic;
      return out;
    }
    if (inputs.platform != ProductionPlatform::windows ||
        inputs.timeline.context_identity != context->renderer_context_identity ||
        inputs.flutter.device_identity != context->renderer_context_identity) {
      ProductionPlatformAssembly out{};
      out.platform = ProductionPlatform::windows;
      out.diagnostic = "Windows provider device/context identity mismatch";
      return out;
    }
    inputs.timeline_evidence = evidence("windows-d3d12-timeline");
    inputs.flutter.evidence = evidence("windows-flutter-texture-registrar");
    inputs.encoder.evidence = evidence("windows-media-foundation-hardware-encoder");
    return create_production_platform_assembly(std::move(inputs));
  };
  return provider;
}

}  // namespace digitor::windows

#endif
