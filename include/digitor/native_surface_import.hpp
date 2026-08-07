#pragma once

#include "digitor/apple_native_zero_copy.hpp"
#include "digitor/native_media.hpp"
#include "digitor/windows_zero_copy_import.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace digitor {

// Additive production routing request. Platform importers are the existing
// backend-owned importers, so imported frames retain their context identity and
// native completion ownership rather than manufacturing an opaque GPU token.
struct NativeSurfaceImportTarget {
  DigitorRendererBackend backend{DIGITOR_RENDERER_AUTO};
  void* native_device{};
  WindowsD3D12ZeroCopyImporter* d3d12{};
  AppleNativeZeroCopyBindings* metal{};
  // Windows Vulkan import must be owned by the renderer because external-memory
  // and external-semaphore objects must be created on the selected VkDevice.
  // The decoder surface must expose a qualified DXGI shared handle.
  NativeMediaImportCallback windows_vulkan{};
  // Android backends own their VkDevice/EGLContext objects. Keeping this as
  // the existing importer callback avoids teaching the platform-neutral bridge
  // how to manufacture resources on a foreign context.
  NativeMediaImportCallback android{};
};

struct NativeSurfaceImportOptions {
  bool strict_zero_copy{true};
  bool require_acquire_sync{};
  DigitorPixelFormat working_format{DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT};
  std::string working_color_space{"linear-rgba"};
};

enum class NativeSurfaceImportFailure : std::uint32_t {
  none,
  cancelled,
  invalid_descriptor,
  null_native_handle,
  invalid_dimensions,
  unsupported_handle,
  unsupported_pixel_format,
  backend_mismatch,
  device_mismatch,
  missing_synchronization,
  invalid_color_metadata,
  backend_unavailable,
  backend_contract_violation
};

struct NativeSurfaceImportResult {
  ProcessedGpuFramePtr frame;
  NativeSurfaceImportFailure failure{NativeSurfaceImportFailure::none};
  DigitorResult result{DIGITOR_RESULT_OK};
  std::string diagnostic;
  [[nodiscard]] explicit operator bool() const noexcept {
    return result == DIGITOR_RESULT_OK && static_cast<bool>(frame);
  }
};

// The only NativeMediaSurface -> ProcessedGpuFrame routing entry point.
// No CPU fallback is performed here. Non-strict decoder fallback remains in the
// decoder before a NativeMediaSurface reaches this boundary.
[[nodiscard]] NativeSurfaceImportResult import_native_media_surface(
    const NativeMediaSurfacePtr& surface,
    const NativeSurfaceImportTarget& target,
    const NativeSurfaceImportOptions& options = {},
    const std::atomic_bool* cancelled = nullptr) noexcept;

} // namespace digitor
