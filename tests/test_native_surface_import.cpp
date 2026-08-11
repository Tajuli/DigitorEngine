#include "digitor/native_surface_import.hpp"

#include <atomic>
#include <cassert>
#include <memory>

using namespace digitor;

namespace {
NativeMediaSurfacePtr surface(NativeMediaHandleType handle = NativeMediaHandleType::dxgi_shared_handle,
                              NativeMediaPixelFormat format = NativeMediaPixelFormat::nv12,
                              NativeMediaPlatform platform = NativeMediaPlatform::windows) {
  NativeMediaSurfaceDescriptor d{};
  d.platform = platform; d.handle_type = handle; d.pixel_format = format;
  d.width = 1920; d.height = 1080; d.plane_count = 2; d.native_handle = 1;
  d.timestamp_us = 42000; d.color.matrix = 1; d.color.chroma_location = 1;
  return std::make_shared<NativeMediaSurface>(d,
      std::static_pointer_cast<void>(std::make_shared<int>(7)));
}

ProcessedGpuFramePtr make_frame(const ZeroCopyImportRequest& request,
                                DigitorRendererBackend backend) {
  static int mock_context;
  const auto& d = request.surface->descriptor();
  GpuFrameMetadata metadata{d.width, d.height, DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT,
                            GpuFrameAlpha::straight, d.timestamp_us, "linear-rgba"};
  return std::make_shared<ProcessedGpuFrame>(
      &mock_context, backend, metadata, 77, request.surface,
      std::make_shared<std::atomic_bool>(true), false);
}
}

int main() {
  NativeSurfaceImportTarget d3d{}; d3d.backend = DIGITOR_RENDERER_D3D12;
  auto r = import_native_media_surface({}, d3d);
  assert(r.failure == NativeSurfaceImportFailure::invalid_descriptor && !r.frame);

  r = import_native_media_surface(surface(NativeMediaHandleType::metal_texture), d3d);
  assert(r.failure == NativeSurfaceImportFailure::backend_mismatch && !r.frame);

  r = import_native_media_surface(surface(NativeMediaHandleType::dxgi_shared_handle,
                                          NativeMediaPixelFormat::rgba8), d3d);
  assert(r.failure == NativeSurfaceImportFailure::unsupported_pixel_format && !r.frame);

  NativeSurfaceImportOptions sync{}; sync.require_acquire_sync = true;
  r = import_native_media_surface(surface(), d3d, sync);
  assert(r.failure == NativeSurfaceImportFailure::missing_synchronization && !r.frame);

  std::atomic_bool cancelled{true};
  r = import_native_media_surface(surface(), d3d, {}, &cancelled);
  assert(r.failure == NativeSurfaceImportFailure::cancelled && !r.frame);

  // Windows DXGI shared decoder surfaces are a valid Vulkan zero-copy source.
  // The actual import still fails closed until the selected renderer supplies
  // its backend-owned Win32 external-memory callback.
  assert(native_surface_backend_compatible(surface()->descriptor(),
                                           DIGITOR_RENDERER_VULKAN));

  NativeSurfaceImportTarget vk{}; vk.backend = DIGITOR_RENDERER_VULKAN;
  r = import_native_media_surface(surface(), vk);
  assert(r.failure == NativeSurfaceImportFailure::backend_unavailable);
  assert(r.diagnostic.find("Vulkan") != std::string::npos);

  vk.windows_vulkan = [](const ZeroCopyImportRequest& request,
                         ProcessedGpuFramePtr& frame) {
    frame = make_frame(request, DIGITOR_RENDERER_VULKAN);
    return DIGITOR_RESULT_OK;
  };
  r = import_native_media_surface(surface(), vk);
  assert(r && r.frame->backend() == DIGITOR_RENDERER_VULKAN);
  assert(r.frame->metadata().timestamp == 42000);

  // Strict import cannot fall through to CPU: an absent production importer is
  // a structured failure and never a placeholder ProcessedGpuFrame.
  r = import_native_media_surface(surface(), d3d);
  assert(r.failure == NativeSurfaceImportFailure::backend_unavailable);
  assert(!r.frame);

  // This is explicitly a routing contract test, not hardware qualification.
  NativeSurfaceImportTarget android{}; android.backend = DIGITOR_RENDERER_VULKAN;
  android.android = [](const ZeroCopyImportRequest& request,
                       ProcessedGpuFramePtr& frame) {
    frame = make_frame(request, DIGITOR_RENDERER_VULKAN);
    return DIGITOR_RESULT_OK;
  };
  r=import_native_media_surface(surface(NativeMediaHandleType::ahardware_buffer,
      NativeMediaPixelFormat::nv12,NativeMediaPlatform::android),android);
  assert(r && r.frame->backend()==DIGITOR_RENDERER_VULKAN);
  return 0;
}
