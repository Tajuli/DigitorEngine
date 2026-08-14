#pragma once

#include "digitor/digitor.h"
#include "digitor/gpu_frame.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

namespace digitor {

enum class NativeMediaPlatform : std::uint32_t { none=0,windows=1,android=2,apple=3,vulkan=4 };
enum class NativeMediaHandleType : std::uint32_t {
  none=0,d3d11_texture2d=1,d3d12_resource=2,dxgi_shared_handle=3,
  ahardware_buffer=10,android_surface_texture=11,
  cv_pixel_buffer=20,io_surface=21,metal_texture=22,
  vulkan_image=30,vulkan_external_memory=31
};
enum class NativeMediaPixelFormat : std::uint32_t {
  unknown=0,nv12=1,p010=2,yuv420p=3,yuv420p10=4,bgra8=5,rgba8=6,rgba16f=7,rgba32f=8
};
enum class NativeMediaSyncType : std::uint32_t {
  none=0,d3d11_fence=1,d3d12_fence=2,metal_shared_event=3,vulkan_semaphore=4,sync_fd=5
};

struct NativeMediaSync { NativeMediaSyncType type{NativeMediaSyncType::none}; std::uintptr_t handle{}; std::uint64_t value{}; };
struct NativeMediaColorMetadata {
  std::int32_t primaries{},transfer{},matrix{};
  std::uint8_t full_range{},chroma_location{};
  std::uint16_t reserved{};
};
struct NativeMediaSurfaceDescriptor {
  std::uint32_t struct_size{};
  std::uint32_t api_version{1};
  NativeMediaPlatform platform{NativeMediaPlatform::none};
  NativeMediaHandleType handle_type{NativeMediaHandleType::none};
  NativeMediaPixelFormat pixel_format{NativeMediaPixelFormat::unknown};
  std::uint32_t width{},height{},plane_count{},array_slice{};
  std::uintptr_t native_handle{},native_device{};
  std::uint64_t allocation_size{};
  std::uint32_t adapter_luid_low{};
  std::int32_t adapter_luid_high{};
  std::int64_t timestamp_us{};
  NativeMediaSync acquire_sync{};
  NativeMediaColorMetadata color{};
};

class NativeMediaSurface final {
public:
  using Owner=std::shared_ptr<void>;
  using ReleaseCallback=std::function<void(const NativeMediaSync&)>;
  NativeMediaSurface(NativeMediaSurfaceDescriptor descriptor,Owner owner,ReleaseCallback release={})
      :descriptor_(descriptor),owner_(std::move(owner)),release_(std::move(release)){
    if(descriptor_.struct_size==0)descriptor_.struct_size=sizeof(NativeMediaSurfaceDescriptor);
    if(descriptor_.struct_size<sizeof(NativeMediaSurfaceDescriptor)||descriptor_.api_version!=1||
       descriptor_.width==0||descriptor_.height==0||descriptor_.native_handle==0||!owner_)
      throw std::invalid_argument("invalid native media surface");
  }
  ~NativeMediaSurface(){release_to_decoder();}
  NativeMediaSurface(const NativeMediaSurface&)=delete;
  NativeMediaSurface&operator=(const NativeMediaSurface&)=delete;
  [[nodiscard]]const NativeMediaSurfaceDescriptor&descriptor()const noexcept{return descriptor_;}
  [[nodiscard]]const Owner&owner()const noexcept{return owner_;}
  [[nodiscard]]bool released()const noexcept{return released_.load(std::memory_order_acquire);}
  void release_to_decoder(NativeMediaSync sync={})noexcept{
    if(released_.exchange(true,std::memory_order_acq_rel))return;
    release_sync_=sync;
    if(release_)release_(release_sync_);
  }
private:
  NativeMediaSurfaceDescriptor descriptor_{};
  Owner owner_;
  ReleaseCallback release_;
  NativeMediaSync release_sync_{};
  std::atomic_bool released_{false};
};
using NativeMediaSurfacePtr=std::shared_ptr<NativeMediaSurface>;

struct ZeroCopyImportRequest {
  NativeMediaSurfacePtr surface;
  DigitorRendererBackend renderer_backend{DIGITOR_RENDERER_AUTO};
  DigitorPixelFormat output_format{DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT};
  std::string working_color_space{"linear-rgba"};
  // Optional internal diagnostic sink. Backend-owned importers populate it
  // with the first native failure; it is not part of the stable C ABI.
  std::string* diagnostic{};
};
using NativeMediaImportCallback=std::function<DigitorResult(const ZeroCopyImportRequest&,ProcessedGpuFramePtr&)>;

[[nodiscard]]inline bool native_surface_backend_compatible(const NativeMediaSurfaceDescriptor&s,DigitorRendererBackend b)noexcept{
  switch(b){
    case DIGITOR_RENDERER_D3D12:return s.platform==NativeMediaPlatform::windows&&
      (s.handle_type==NativeMediaHandleType::d3d11_texture2d||s.handle_type==NativeMediaHandleType::d3d12_resource||s.handle_type==NativeMediaHandleType::dxgi_shared_handle);
    case DIGITOR_RENDERER_METAL:return s.platform==NativeMediaPlatform::apple&&
      (s.handle_type==NativeMediaHandleType::cv_pixel_buffer||s.handle_type==NativeMediaHandleType::io_surface||s.handle_type==NativeMediaHandleType::metal_texture);
    case DIGITOR_RENDERER_VULKAN:return s.handle_type==NativeMediaHandleType::vulkan_image||s.handle_type==NativeMediaHandleType::vulkan_external_memory||s.handle_type==NativeMediaHandleType::ahardware_buffer||
      (s.platform==NativeMediaPlatform::windows&&s.handle_type==NativeMediaHandleType::dxgi_shared_handle);
    case DIGITOR_RENDERER_OPENGL_ES:return s.platform==NativeMediaPlatform::android&&
      (s.handle_type==NativeMediaHandleType::ahardware_buffer||s.handle_type==NativeMediaHandleType::android_surface_texture);
    default:return false;
  }
}

class ZeroCopyMediaImporter final {
public:
  explicit ZeroCopyMediaImporter(NativeMediaImportCallback callback):callback_(std::move(callback)){
    if(!callback_)throw std::invalid_argument("zero-copy importer callback is required");
  }
  [[nodiscard]]DigitorResult import(const ZeroCopyImportRequest&r,ProcessedGpuFramePtr&out)const noexcept{
    out.reset();
    if(!r.surface||!native_surface_backend_compatible(r.surface->descriptor(),r.renderer_backend))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    try{
      const auto result=callback_(r,out);
      if(result!=DIGITOR_RESULT_OK)out.reset();
      return result==DIGITOR_RESULT_OK&&!out?DIGITOR_RESULT_INTERNAL_ERROR:result;
    }catch(const std::bad_alloc&){out.reset();return DIGITOR_RESULT_OUT_OF_MEMORY;}
    catch(...){out.reset();return DIGITOR_RESULT_INTERNAL_ERROR;}
  }
private:NativeMediaImportCallback callback_;
};

} // namespace digitor
