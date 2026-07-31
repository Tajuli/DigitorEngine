#pragma once

#include "digitor/digitor.h"
#include "digitor/gpu_frame.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

namespace digitor {

// Decoder-owned surfaces that can be imported by the render backend without a
// CPU readback. The native object is deliberately opaque at the public C++ API.
enum class NativeMediaPlatform : std::uint32_t {
  none = 0,
  windows = 1,
  android = 2,
  apple = 3,
  vulkan = 4
};

enum class NativeMediaHandleType : std::uint32_t {
  none = 0,
  d3d11_texture2d = 1,
  d3d12_resource = 2,
  dxgi_shared_handle = 3,
  ahardware_buffer = 10,
  android_surface_texture = 11,
  cv_pixel_buffer = 20,
  io_surface = 21,
  metal_texture = 22,
  vulkan_image = 30,
  vulkan_external_memory = 31
};

enum class NativeMediaPixelFormat : std::uint32_t {
  unknown = 0,
  nv12 = 1,
  p010 = 2,
  yuv420p = 3,
  yuv420p10 = 4,
  bgra8 = 5,
  rgba8 = 6,
  rgba16f = 7,
  rgba32f = 8
};

enum class NativeMediaSyncType : std::uint32_t {
  none = 0,
  d3d11_fence = 1,
  d3d12_fence = 2,
  metal_shared_event = 3,
  vulkan_semaphore = 4,
  sync_fd = 5
};

struct NativeMediaSync {
  NativeMediaSyncType type{NativeMediaSyncType::none};
  std::uintptr_t handle{};
  std::uint64_t value{};
};

struct NativeMediaColorMetadata {
  std::int32_t primaries{};
  std::int32_t transfer{};
  std::int32_t matrix{};
  std::uint8_t full_range{};
  std::uint8_t chroma_location{};
  std::uint16_t reserved{};
};

struct NativeMediaSurfaceDescriptor {
  std::uint32_t struct_size{sizeof(NativeMediaSurfaceDescriptor)};
  std::uint32_t api_version{1};
  NativeMediaPlatform platform{NativeMediaPlatform::none};
  NativeMediaHandleType handle_type{NativeMediaHandleType::none};
  NativeMediaPixelFormat pixel_format{NativeMediaPixelFormat::unknown};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t plane_count{};
  std::uint32_t array_slice{};
  std::uintptr_t native_handle{};
  std::uintptr_t native_device{};
  std::uint64_t allocation_size{};
  std::int64_t timestamp_us{};
  NativeMediaSync acquire_sync{};
  NativeMediaColorMetadata color{};
};

class NativeMediaSurface final {
public:
  using Owner = std::shared_ptr<void>;
  using ReleaseCallback = std::function<void(const NativeMediaSync&)>;

  NativeMediaSurface(NativeMediaSurfaceDescriptor descriptor, Owner owner,
                     ReleaseCallback release = {})
      : descriptor_(descriptor), owner_(std::move(owner)),
        release_(std::move(release)) {
    if (descriptor_.struct_size < sizeof(NativeMediaSurfaceDescriptor) ||
        descriptor_.api_version != 1 || descriptor_.width == 0 ||
        descriptor_.height == 0 || descriptor_.native_handle == 0 || !owner_) {
      throw std::invalid_argument("invalid native media surface");
    }
  }

  ~NativeMediaSurface() {
    if (release_ && !released_.exchange(true, std::memory_order_acq_rel)) {
      release_(release_sync_);
    }
  }

  NativeMediaSurface(const NativeMediaSurface&) = delete;
  NativeMediaSurface& operator=(const NativeMediaSurface&) = delete;

  [[nodiscard]] const NativeMediaSurfaceDescriptor& descriptor() const noexcept {
    return descriptor_;
  }
  [[nodiscard]] const Owner& owner() const noexcept { return owner_; }
  [[nodiscard]] bool released() const noexcept {
    return released_.load(std::memory_order_acquire);
  }

  // The importing backend supplies the fence/semaphore that protects decoder
  // surface reuse. Release is idempotent and normally happens at destruction.
  void release_to_decoder(NativeMediaSync sync = {}) noexcept {
    release_sync_ = sync;
    if (release_ && !released_.exchange(true, std::memory_order_acq_rel)) {
      release_(release_sync_);
    }
  }

private:
  NativeMediaSurfaceDescriptor descriptor_{};
  Owner owner_;
  ReleaseCallback release_;
  NativeMediaSync release_sync_{};
  std::atomic_bool released_{false};
};

using NativeMediaSurfacePtr = std::shared_ptr<NativeMediaSurface>;

struct ZeroCopyImportRequest {
  NativeMediaSurfacePtr surface;
  DigitorRendererBackend renderer_backend{DIGITOR_RENDERER_AUTO};
  DigitorPixelFormat output_format{DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT};
  std::string working_color_space{"linear-rgba"};
};

using NativeMediaImportCallback =
    std::function<DigitorResult(const ZeroCopyImportRequest&,
                                ProcessedGpuFramePtr&)>;

[[nodiscard]] inline bool native_surface_backend_compatible(
    const NativeMediaSurfaceDescriptor& surface,
    DigitorRendererBackend backend) noexcept {
  switch (backend) {
    case DIGITOR_RENDERER_D3D12:
      return surface.platform == NativeMediaPlatform::windows &&
             (surface.handle_type == NativeMediaHandleType::d3d11_texture2d ||
              surface.handle_type == NativeMediaHandleType::d3d12_resource ||
              surface.handle_type == NativeMediaHandleType::dxgi_shared_handle);
    case DIGITOR_RENDERER_METAL:
      return surface.platform == NativeMediaPlatform::apple &&
             (surface.handle_type == NativeMediaHandleType::cv_pixel_buffer ||
              surface.handle_type == NativeMediaHandleType::io_surface ||
              surface.handle_type == NativeMediaHandleType::metal_texture);
    case DIGITOR_RENDERER_VULKAN:
      return surface.handle_type == NativeMediaHandleType::vulkan_image ||
             surface.handle_type == NativeMediaHandleType::vulkan_external_memory ||
             surface.handle_type == NativeMediaHandleType::ahardware_buffer;
    case DIGITOR_RENDERER_OPENGL_ES:
      return surface.platform == NativeMediaPlatform::android &&
             (surface.handle_type == NativeMediaHandleType::ahardware_buffer ||
              surface.handle_type == NativeMediaHandleType::android_surface_texture);
    default:
      return false;
  }
}

// Strict adapter: a native frame can never silently enter the legacy CPU upload
// path. Unsupported interop is reported to the caller, which may explicitly
// request a separately decoded CPU frame when policy allows it.
class ZeroCopyMediaImporter final {
public:
  explicit ZeroCopyMediaImporter(NativeMediaImportCallback callback)
      : callback_(std::move(callback)) {
    if (!callback_) throw std::invalid_argument("zero-copy importer callback is required");
  }

  [[nodiscard]] DigitorResult import(const ZeroCopyImportRequest& request,
                                     ProcessedGpuFramePtr& out) const noexcept {
    out.reset();
    if (!request.surface ||
        !native_surface_backend_compatible(request.surface->descriptor(),
                                           request.renderer_backend)) {
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    try {
      const auto result = callback_(request, out);
      if (result != DIGITOR_RESULT_OK) out.reset();
      return result == DIGITOR_RESULT_OK && !out
                 ? DIGITOR_RESULT_INTERNAL_ERROR
                 : result;
    } catch (const std::bad_alloc&) {
      out.reset();
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      out.reset();
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
  }

private:
  NativeMediaImportCallback callback_;
};

} // namespace digitor
