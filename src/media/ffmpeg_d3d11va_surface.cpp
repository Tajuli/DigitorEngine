#include "digitor/ffmpeg_d3d11va_surface.hpp"

#include <memory>
#include <new>

#if defined(_WIN32) && defined(DIGITOR_HAS_FFMPEG)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}
#endif

namespace digitor {

#if defined(_WIN32) && defined(DIGITOR_HAS_FFMPEG)
namespace {
using Microsoft::WRL::ComPtr;

struct SharedHandleOwner {
  AVFrame* frame{};
  ComPtr<ID3D11Texture2D> texture;
  HANDLE handle{};
  ~SharedHandleOwner() {
    if (handle) CloseHandle(handle);
    av_frame_free(&frame);
  }
};

WindowsYuvMatrix matrix_from_av(int value) noexcept {
  // FFmpeg AVCOL_SPC_* numeric values are intentionally handled without
  // exposing FFmpeg enums in the public header.
  switch (value) {
    case 1: return WindowsYuvMatrix::bt709;
    case 9: return WindowsYuvMatrix::bt2020_ncl;
    default: return WindowsYuvMatrix::bt601;
  }
}

WindowsChromaSiting chroma_from_av(int value) noexcept {
  // AVCOL_CHROMA_LOC_LEFT=1, CENTER=2, TOPLEFT=3.
  switch (value) {
    case 2: return WindowsChromaSiting::center;
    case 3: return WindowsChromaSiting::top_left;
    default: return WindowsChromaSiting::left;
  }
}

DigitorResult hr_result(HRESULT hr) noexcept {
  if (hr == E_OUTOFMEMORY) return DIGITOR_RESULT_OUT_OF_MEMORY;
  return FAILED(hr) ? DIGITOR_RESULT_BACKEND_UNAVAILABLE
                    : DIGITOR_RESULT_OK;
}

bool make_shareable_slice_copy(ID3D11Texture2D* source,
                               std::uint32_t source_slice,
                               ComPtr<ID3D11Texture2D>& destination,
                               std::string& diagnostic) {
  D3D11_TEXTURE2D_DESC source_desc{};
  source->GetDesc(&source_desc);
  if (source_slice >= source_desc.ArraySize || source_desc.MipLevels == 0) {
    diagnostic = "D3D11VA decoder array slice is out of range";
    return false;
  }
  ComPtr<ID3D11Device> device;
  source->GetDevice(&device);
  if (!device) {
    diagnostic = "D3D11VA texture has no owning device";
    return false;
  }
  auto destination_desc = source_desc;
  destination_desc.ArraySize = 1;
  destination_desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
  destination_desc.MiscFlags &= ~D3D11_RESOURCE_MISC_SHARED;
  destination_desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
  destination_desc.CPUAccessFlags = 0;
  destination_desc.Usage = D3D11_USAGE_DEFAULT;
  const HRESULT hr = device->CreateTexture2D(&destination_desc, nullptr, &destination);
  if (FAILED(hr) || !destination) {
    diagnostic = "failed to create normalized shareable D3D11 decoder texture";
    return false;
  }
  ComPtr<ID3D11DeviceContext> context;
  device->GetImmediateContext(&context);
  if (!context) {
    diagnostic = "D3D11VA device has no immediate context";
    destination.Reset();
    return false;
  }
  const UINT source_subresource = D3D11CalcSubresource(0, source_slice, source_desc.MipLevels);
  context->CopySubresourceRegion(destination.Get(), 0, 0, 0, 0,
                                 source, source_subresource, nullptr);
  return true;
}

bool create_nt_handle(ID3D11Texture2D* texture, HANDLE& handle,
                      std::string& diagnostic) {
  ComPtr<IDXGIResource1> resource;
  HRESULT hr = texture->QueryInterface(IID_PPV_ARGS(&resource));
  if (FAILED(hr) || !resource) {
    diagnostic = "D3D11 texture does not expose IDXGIResource1";
    return false;
  }
  hr = resource->CreateSharedHandle(nullptr,
      DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
      nullptr, &handle);
  if (FAILED(hr) || !handle) {
    diagnostic = "IDXGIResource1::CreateSharedHandle failed";
    return false;
  }
  return true;
}

NativeMediaPixelFormat native_format(DXGI_FORMAT format) noexcept {
  return format == DXGI_FORMAT_P010 ? NativeMediaPixelFormat::p010
                                    : NativeMediaPixelFormat::nv12;
}

WindowsZeroCopyFormat windows_format(DXGI_FORMAT format) noexcept {
  return format == DXGI_FORMAT_P010 ? WindowsZeroCopyFormat::p010
                                    : WindowsZeroCopyFormat::nv12;
}

} // namespace
#endif

namespace {
DigitorResult extract_ffmpeg_d3d11va_surface_impl(
    void* opaque_frame,
    std::int64_t timestamp_us,
    bool normalized_timestamp,
    FfmpegD3D11vaExtractionResult& out) noexcept {
  out = {};
#if !defined(_WIN32) || !defined(DIGITOR_HAS_FFMPEG)
  (void)opaque_frame;
  (void)timestamp_us;
  (void)normalized_timestamp;
  out.diagnostic = "FFmpeg D3D11VA extraction is unavailable in this build";
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  if (!opaque_frame) {
    out.diagnostic = "AVFrame pointer is null";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  auto* frame = static_cast<AVFrame*>(opaque_frame);
  if (frame->format != AV_PIX_FMT_D3D11 || !frame->data[0]) {
    out.diagnostic = "AVFrame is not an AV_PIX_FMT_D3D11 surface";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  out.frame_is_d3d11 = true;

  try {
    auto owner = std::make_shared<SharedHandleOwner>();
    owner->frame = av_frame_alloc();
    if (!owner->frame) return DIGITOR_RESULT_OUT_OF_MEMORY;
    const int ref_result = av_frame_ref(owner->frame, frame);
    if (ref_result < 0) {
      out.diagnostic = "av_frame_ref failed for D3D11VA surface";
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    out.texture_retained = true;

    auto* texture = reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
    const auto slice = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(frame->data[1]));
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if ((desc.Format != DXGI_FORMAT_NV12 && desc.Format != DXGI_FORMAT_P010) ||
        !frame->width || !frame->height || slice >= desc.ArraySize) {
      out.diagnostic = "D3D11VA surface is not a valid NV12/P010 array slice";
      return DIGITOR_RESULT_UNSUPPORTED;
    }

    owner->texture = texture;
    HANDLE shared{};
    std::uint32_t exported_slice = slice;
    if (desc.ArraySize == 1 && slice == 0 &&
        create_nt_handle(owner->texture.Get(), shared, out.diagnostic)) {
      out.shareable_texture_reused = true;
      exported_slice = 0;
    } else {
      out.diagnostic.clear();
      ComPtr<ID3D11Texture2D> copy;
      if (!make_shareable_slice_copy(texture, slice, copy, out.diagnostic))
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      owner->texture = std::move(copy);
      if (!create_nt_handle(owner->texture.Get(), shared, out.diagnostic))
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      out.shareable_copy_created = true;
      exported_slice = 0;
    }
    owner->handle = shared;
    out.shared_handle_created = true;
    out.no_cpu_transfer = true;

    NativeMediaSurfaceDescriptor native{};
    native.platform = NativeMediaPlatform::windows;
    native.handle_type = NativeMediaHandleType::dxgi_shared_handle;
    native.pixel_format = native_format(desc.Format);
    native.width = static_cast<std::uint32_t>(frame->width);
    native.height = static_cast<std::uint32_t>(frame->height);
    native.plane_count = 2;
    native.array_slice = exported_slice;
    native.native_handle = reinterpret_cast<std::uintptr_t>(shared);
    native.native_device = reinterpret_cast<std::uintptr_t>(frame->hw_frames_ctx);
    native.timestamp_us = normalized_timestamp ? timestamp_us : frame->best_effort_timestamp;
    native.color.primaries = frame->color_primaries;
    native.color.transfer = frame->color_trc;
    native.color.matrix = frame->colorspace;
    native.color.full_range = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    native.color.chroma_location = static_cast<std::uint8_t>(frame->chroma_location);

    auto native_surface = std::make_shared<NativeMediaSurface>(
        native, std::static_pointer_cast<void>(owner));

    out.surface.format = windows_format(desc.Format);
    out.surface.width = static_cast<std::uint32_t>(frame->width);
    out.surface.height = static_cast<std::uint32_t>(frame->height);
    out.surface.array_slice = exported_slice;
    out.surface.shared_handle = reinterpret_cast<std::uintptr_t>(shared);
    out.surface.decoder_device = reinterpret_cast<std::uintptr_t>(frame->hw_frames_ctx);
    out.surface.timestamp_us = native.timestamp_us;
    out.surface.color.matrix = matrix_from_av(frame->colorspace);
    out.surface.color.chroma_siting = chroma_from_av(frame->chroma_location);
    out.surface.color.full_range = frame->color_range == AVCOL_RANGE_JPEG;
    out.surface.color.primaries = frame->color_primaries;
    out.surface.color.transfer = frame->color_trc;
    out.surface.lifetime = std::move(native_surface);
    out.diagnostic.clear();
    return DIGITOR_RESULT_OK;
  } catch (const std::bad_alloc&) {
    out = {};
    out.diagnostic = "out of memory while retaining D3D11VA surface";
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    out = {};
    out.diagnostic = "unexpected FFmpeg D3D11VA extraction failure";
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
#endif
}
} // namespace

DigitorResult extract_ffmpeg_d3d11va_surface(
    void* opaque_frame,
    FfmpegD3D11vaExtractionResult& out) noexcept {
  return extract_ffmpeg_d3d11va_surface_impl(
      opaque_frame, 0, false, out);
}

DigitorResult extract_ffmpeg_d3d11va_surface(
    void* opaque_frame,
    std::int64_t timestamp_us,
    FfmpegD3D11vaExtractionResult& out) noexcept {
  return extract_ffmpeg_d3d11va_surface_impl(
      opaque_frame, timestamp_us, true, out);
}

} // namespace digitor
