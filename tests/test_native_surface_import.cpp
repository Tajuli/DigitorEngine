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

  NativeSurfaceImportTarget vk{}; vk.backend = DIGITOR_RENDERER_VULKAN;
  r = import_native_media_surface(surface(), vk);
  assert(r.failure == NativeSurfaceImportFailure::backend_mismatch);
  assert(r.diagnostic.find("Vulkan") != std::string::npos);

  // Strict import cannot fall through to CPU: an absent production importer is
  // a structured failure and never a placeholder ProcessedGpuFrame.
  r = import_native_media_surface(surface(), d3d);
  assert(r.failure == NativeSurfaceImportFailure::backend_unavailable);
  assert(!r.frame);
  return 0;
}
