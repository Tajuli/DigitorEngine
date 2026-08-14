#include "digitor/native_surface_import.hpp"

#include <new>

namespace digitor {
namespace {
NativeSurfaceImportResult fail(NativeSurfaceImportFailure failure,
                               DigitorResult result, const char* diagnostic) {
  return {{}, failure, result, diagnostic};
}

bool format_supported(NativeMediaPixelFormat format) noexcept {
  return format == NativeMediaPixelFormat::nv12 ||
         format == NativeMediaPixelFormat::p010;
}

bool color_valid(const NativeMediaColorMetadata& color) noexcept {
  // FFmpeg's UNSPECIFIED values are zero. Negative/out-of-range values are not
  // safe inputs to the GPU colour conversion contracts.
  return color.primaries >= 0 && color.transfer >= 0 && color.matrix >= 0 &&
         color.primaries <= 255 && color.transfer <= 255 &&
         color.matrix <= 255 && color.full_range <= 1 &&
         color.chroma_location <= 6;
}

bool windows_vulkan_surface(const NativeMediaSurfaceDescriptor& d,
                            DigitorRendererBackend backend) noexcept {
  return backend == DIGITOR_RENDERER_VULKAN &&
         d.platform == NativeMediaPlatform::windows &&
         d.handle_type == NativeMediaHandleType::dxgi_shared_handle;
}

WindowsYuvMatrix windows_matrix(std::int32_t value) noexcept {
  if (value == 9) return WindowsYuvMatrix::bt2020_ncl;
  if (value == 1) return WindowsYuvMatrix::bt709;
  return WindowsYuvMatrix::bt601;
}
WindowsChromaSiting windows_chroma(std::uint8_t value) noexcept {
  if (value == 2) return WindowsChromaSiting::center;
  if (value == 3) return WindowsChromaSiting::top_left;
  return WindowsChromaSiting::left;
}
#if defined(__APPLE__)
AppleColorMatrix apple_matrix(std::int32_t value) noexcept {
  if (value == 9) return AppleColorMatrix::bt2020_ncl;
  if (value == 1) return AppleColorMatrix::bt709;
  return AppleColorMatrix::bt601;
}
#endif
} // namespace

NativeSurfaceImportResult import_native_media_surface(
    const NativeMediaSurfacePtr& surface,
    const NativeSurfaceImportTarget& target,
    const NativeSurfaceImportOptions& options,
    const std::atomic_bool* cancelled) noexcept {
  try {
    if (cancelled && cancelled->load(std::memory_order_acquire))
      return fail(NativeSurfaceImportFailure::cancelled,
                  DIGITOR_RESULT_RESOURCE_IN_USE, "native surface import cancelled");
    if (!surface)
      return fail(NativeSurfaceImportFailure::invalid_descriptor,
                  DIGITOR_RESULT_INVALID_ARGUMENT, "native surface is null");
    const auto& d = surface->descriptor();
    if (d.struct_size < sizeof(NativeMediaSurfaceDescriptor) || d.api_version != 1)
      return fail(NativeSurfaceImportFailure::invalid_descriptor,
                  DIGITOR_RESULT_INVALID_ARGUMENT,
                  "unsupported native surface descriptor size or API version");
    if (!d.native_handle)
      return fail(NativeSurfaceImportFailure::null_native_handle,
                  DIGITOR_RESULT_INVALID_ARGUMENT, "native handle is null");
    if (!d.width || !d.height || (d.width & 1u) || (d.height & 1u) ||
        d.plane_count != 2)
      return fail(NativeSurfaceImportFailure::invalid_dimensions,
                  DIGITOR_RESULT_INVALID_ARGUMENT,
                  "NV12/P010 import requires even dimensions and two planes");
    if (!format_supported(d.pixel_format))
      return fail(NativeSurfaceImportFailure::unsupported_pixel_format,
                  DIGITOR_RESULT_UNSUPPORTED,
                  "only native NV12 and P010 surfaces are supported");
    const bool compatible = native_surface_backend_compatible(d, target.backend) ||
                            windows_vulkan_surface(d, target.backend);
    if (target.backend == DIGITOR_RENDERER_AUTO || !compatible)
      return fail(NativeSurfaceImportFailure::backend_mismatch,
                  DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                  "native surface is incompatible with the selected backend");
    if (!color_valid(d.color))
      return fail(NativeSurfaceImportFailure::invalid_color_metadata,
                  DIGITOR_RESULT_INVALID_ARGUMENT, "invalid native colour metadata");
    if (options.working_format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT)
      return fail(NativeSurfaceImportFailure::unsupported_pixel_format,
                  DIGITOR_RESULT_UNSUPPORTED,
                  "native YUV import currently targets RGBA16F working frames");
    if (options.require_acquire_sync && d.acquire_sync.type == NativeMediaSyncType::none)
      return fail(NativeSurfaceImportFailure::missing_synchronization,
                  DIGITOR_RESULT_INVALID_ARGUMENT,
                  "an acquire synchronization primitive is required");

    ProcessedGpuFramePtr frame;
    DigitorResult result = DIGITOR_RESULT_UNSUPPORTED;
    if (target.backend == DIGITOR_RENDERER_D3D12) {
      if (!target.d3d12)
        return fail(NativeSurfaceImportFailure::backend_unavailable,
                    DIGITOR_RESULT_NOT_INITIALIZED,
                    "D3D12 native surface importer is not initialized");
      if (d.handle_type != NativeMediaHandleType::dxgi_shared_handle)
        return fail(NativeSurfaceImportFailure::unsupported_handle,
                    DIGITOR_RESULT_UNSUPPORTED,
                    "D3D12 import requires a qualified DXGI shared handle");
      WindowsZeroCopySurface in;
      in.format = d.pixel_format == NativeMediaPixelFormat::p010
                    ? WindowsZeroCopyFormat::p010 : WindowsZeroCopyFormat::nv12;
      in.width = d.width; in.height = d.height; in.array_slice = d.array_slice;
      in.shared_handle = d.native_handle; in.decoder_device = d.native_device;
      in.timestamp_us = d.timestamp_us; in.lifetime = surface;
      in.color.matrix = windows_matrix(d.color.matrix);
      in.color.chroma_siting = windows_chroma(d.color.chroma_location);
      in.color.full_range = d.color.full_range != 0;
      in.color.primaries = d.color.primaries; in.color.transfer = d.color.transfer;
      WindowsZeroCopyQualification qualification;
      result = target.d3d12->import(in, frame, &qualification);
      if (result != DIGITOR_RESULT_OK)
        return fail(NativeSurfaceImportFailure::backend_unavailable, result,
                    qualification.diagnostic.empty()
                        ? "D3D12 native import failed without CPU fallback"
                        : qualification.diagnostic.c_str());
    } else if (target.backend == DIGITOR_RENDERER_METAL) {
#if !defined(__APPLE__)
      return fail(NativeSurfaceImportFailure::backend_unavailable,
                  DIGITOR_RESULT_UNSUPPORTED,
                  "Metal native surface import is unavailable on this host");
#else
      if (!target.metal)
        return fail(NativeSurfaceImportFailure::backend_unavailable,
                    DIGITOR_RESULT_NOT_INITIALIZED,
                    "Metal native surface importer is not initialized");
      if (d.handle_type != NativeMediaHandleType::cv_pixel_buffer)
        return fail(NativeSurfaceImportFailure::unsupported_handle,
                    DIGITOR_RESULT_UNSUPPORTED,
                    "Metal import requires a CVPixelBuffer");
      ApplePixelBufferFrame in;
      in.pixel_buffer = reinterpret_cast<void*>(d.native_handle);
      const bool p010 = d.pixel_format == NativeMediaPixelFormat::p010;
      in.format = p010 ? (d.color.full_range ? AppleYuvFormat::p010_full : AppleYuvFormat::p010_video)
                       : (d.color.full_range ? AppleYuvFormat::nv12_full : AppleYuvFormat::nv12_video);
      in.matrix = apple_matrix(d.color.matrix); in.width = d.width; in.height = d.height;
      in.bit_depth = p010 ? 10u : 8u; in.timestamp_us = d.timestamp_us;
      in.frame_identity = static_cast<std::uint64_t>(d.timestamp_us);
      in.decoder_lifetime = surface;
      AppleMetalImportedFrame imported;
      result = target.metal->import_metal(in, imported);
      if (result == DIGITOR_RESULT_OK) result = target.metal->convert(imported, frame);
#endif
    } else if (target.backend == DIGITOR_RENDERER_VULKAN &&
               d.platform == NativeMediaPlatform::windows) {
      if (d.handle_type != NativeMediaHandleType::dxgi_shared_handle)
        return fail(NativeSurfaceImportFailure::unsupported_handle,
                    DIGITOR_RESULT_UNSUPPORTED,
                    "Windows Vulkan import requires a qualified DXGI shared handle");
      if (!target.windows_vulkan)
        return fail(NativeSurfaceImportFailure::backend_unavailable,
                    DIGITOR_RESULT_NOT_INITIALIZED,
                    "Windows Vulkan external-memory importer is not initialized");
      ZeroCopyImportRequest request{surface, target.backend,
                                    options.working_format,
                                    options.working_color_space};
      result = target.windows_vulkan(request, frame);
    } else if (target.backend == DIGITOR_RENDERER_VULKAN ||
               target.backend == DIGITOR_RENDERER_OPENGL_ES) {
      if (d.platform != NativeMediaPlatform::android ||
          d.handle_type != NativeMediaHandleType::ahardware_buffer)
        return fail(NativeSurfaceImportFailure::unsupported_handle,
                    DIGITOR_RESULT_UNSUPPORTED,
                    "Android GPU import requires a retained AHardwareBuffer");
      if (!target.android)
        return fail(NativeSurfaceImportFailure::backend_unavailable,
                    DIGITOR_RESULT_NOT_INITIALIZED,
                    "Android backend-owned native importer is not initialized");
      ZeroCopyImportRequest request{surface, target.backend,
                                    options.working_format,
                                    options.working_color_space};
      result = target.android(request, frame);
    }
    if (result != DIGITOR_RESULT_OK)
      return fail(NativeSurfaceImportFailure::backend_unavailable, result,
                  "native backend import failed without CPU fallback");
    if (!frame || frame->backend() != target.backend || !frame->ready() ||
        frame->metadata().width != d.width || frame->metadata().height != d.height ||
        frame->metadata().timestamp != d.timestamp_us ||
        frame->metadata().format != options.working_format)
      return fail(NativeSurfaceImportFailure::backend_contract_violation,
                  DIGITOR_RESULT_INTERNAL_ERROR,
                  "backend returned a frame that violates the import contract");
    return {std::move(frame), NativeSurfaceImportFailure::none,
            DIGITOR_RESULT_OK, {}};
  } catch (const std::bad_alloc&) {
    return fail(NativeSurfaceImportFailure::backend_unavailable,
                DIGITOR_RESULT_OUT_OF_MEMORY, "native surface import ran out of memory");
  } catch (...) {
    return fail(NativeSurfaceImportFailure::backend_unavailable,
                DIGITOR_RESULT_INTERNAL_ERROR, "unexpected native surface import failure");
  }
}
} // namespace digitor
