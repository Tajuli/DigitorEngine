#pragma once

#include "digitor/native_platform_provider.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace digitor {

// The stock Flutter Windows pixel-buffer texture API is intentionally rejected
// because it requires CPU-readable pixels. A production Digitor provider must
// supply an external GPU-texture registrar extension that retains the exact
// D3D12/Vulkan-backed ProcessedGpuFrame until Flutter consumption completes.
struct WindowsExternalGpuTextureApi final {
  std::string implementation_identity;
  const void* d3d_device_identity{};
  std::function<bool()> attached;
  std::function<DigitorResult(const ProcessedGpuFramePtr&, std::uint64_t)>
      register_or_present;
  bool accepts_d3d12_resource{};
  bool accepts_vulkan_external_memory{};
  bool retains_until_consumed{};
  bool native_fence_wait_bound{};
  bool zero_cpu_readback{};
  bool zero_cpu_staging{};
};

struct WindowsNativeEncoderApi final {
  std::string implementation_identity;
  WindowsHardwareEncoderHost host;
  bool media_foundation_bound{};
  bool nvenc_or_qsv_bound{};
  bool d3d12_resource_input{};
  bool native_fence_wait_bound{};
  bool zero_cpu_readback{};
  bool zero_cpu_staging{};
};

struct WindowsNativeProviderInputs final {
  std::string package_identity;
  std::string build_identity;
  ProductionTimelineGpuHost timeline;
  WindowsExternalGpuTextureApi flutter_texture;
  WindowsNativeEncoderApi encoder;
  WindowsVulkanZeroCopyInterop vulkan_interop;
};

struct WindowsNativeProviderValidation final {
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

[[nodiscard]] WindowsNativeProviderValidation validate_windows_native_provider_inputs(
    const WindowsNativeProviderInputs& inputs) noexcept;

[[nodiscard]] NativePlatformProvider create_windows_native_platform_provider(
    WindowsNativeProviderInputs inputs);

}  // namespace digitor
