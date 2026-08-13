#include "digitor/ffmpeg_d3d11va_surface.hpp"

#include <cctype>
#include <iomanip>
#include <memory>
#include <new>
#include <sstream>
#include <vector>

#if defined(_WIN32) && defined(DIGITOR_HAS_FFMPEG)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3d11sdklayers.h>
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
std::string hresult_name(HRESULT hr) {
  switch (hr) {
  case E_INVALIDARG:
    return "E_INVALIDARG";
  case E_OUTOFMEMORY:
    return "E_OUTOFMEMORY";
  case DXGI_ERROR_INVALID_CALL:
    return "DXGI_ERROR_INVALID_CALL";
  case DXGI_ERROR_UNSUPPORTED:
    return "DXGI_ERROR_UNSUPPORTED";
  case DXGI_ERROR_DEVICE_REMOVED:
    return "DXGI_ERROR_DEVICE_REMOVED";
  case DXGI_ERROR_DEVICE_RESET:
    return "DXGI_ERROR_DEVICE_RESET";
  default:
    break;
  }
  LPSTR message = nullptr;
  const DWORD count = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPSTR>(&message), 0,
      nullptr);
  std::string text =
      count && message ? std::string(message, count) : "unknown HRESULT";
  if (message)
    LocalFree(message);
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
    text.pop_back();
  return text;
}

void append_desc(std::ostringstream &out, const char *name,
                 const D3D11_TEXTURE2D_DESC &d) {
  out << "; " << name << "={Width=" << d.Width << ", Height=" << d.Height
      << ", MipLevels=" << d.MipLevels << ", ArraySize=" << d.ArraySize
      << ", Format=" << static_cast<unsigned>(d.Format)
      << ", SampleDesc.Count=" << d.SampleDesc.Count
      << ", SampleDesc.Quality=" << d.SampleDesc.Quality
      << ", Usage=" << static_cast<unsigned>(d.Usage) << ", BindFlags=0x"
      << std::hex << std::uppercase << d.BindFlags << ", CPUAccessFlags=0x"
      << d.CPUAccessFlags << ", MiscFlags=0x" << d.MiscFlags << std::dec << "}";
}

std::string stable_debug_message(std::string text) {
  // Debug-layer text can embed COM object addresses. Keep validation content
  // actionable while ensuring diagnostics crossing the public ABI are stable.
  for (std::size_t start = 0; start + 2 < text.size();) {
    if (text[start] != '0' ||
        (text[start + 1] != 'x' && text[start + 1] != 'X')) {
      ++start;
      continue;
    }
    std::size_t end = start + 2;
    while (end < text.size() &&
           std::isxdigit(static_cast<unsigned char>(text[end])))
      ++end;
    if (end - (start + 2) >= 12) {
      text.replace(start, end - start, "<object>");
      start += 8;
    } else {
      start = end;
    }
  }
  return text;
}
} // namespace

D3D11_TEXTURE2D_DESC
normalized_d3d11va_interop_desc(const D3D11_TEXTURE2D_DESC &source) noexcept {
  D3D11_TEXTURE2D_DESC destination{};
  destination.Width = source.Width;
  destination.Height = source.Height;
  destination.MipLevels = 1;
  destination.ArraySize = 1;
  destination.Format = source.Format;
  destination.SampleDesc = source.SampleDesc;
  destination.Usage = D3D11_USAGE_DEFAULT;
  destination.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  destination.CPUAccessFlags = 0;
  destination.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
  return destination;
}

std::string format_d3d11_texture_creation_failure(
    HRESULT result, const D3D11_TEXTURE2D_DESC &source,
    const D3D11_TEXTURE2D_DESC &destination, D3D_FEATURE_LEVEL feature_level,
    HRESULT format_support_result, UINT format_support,
    const std::string &debug_message) {
  std::ostringstream out;
  out << "failed to create normalized shareable D3D11 decoder texture: "
         "HRESULT=0x"
      << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
      << static_cast<std::uint32_t>(result) << std::dec << " ("
      << hresult_name(result) << ")"
      << "; feature_level=0x" << std::hex
      << static_cast<unsigned>(feature_level)
      << "; CheckFormatSupport.HRESULT=0x" << std::setw(8)
      << static_cast<std::uint32_t>(format_support_result)
      << "; format_support=0x" << format_support << std::dec;
  append_desc(out, "source", source);
  append_desc(out, "destination", destination);
  if (!debug_message.empty())
    out << "; debug_layer=" << stable_debug_message(debug_message);
  return out.str();
}

namespace {
using Microsoft::WRL::ComPtr;

struct SharedHandleOwner {
  AVFrame *frame{};
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11Fence> acquire_fence;
  HANDLE handle{};
  HANDLE acquire_fence_handle{};
  ~SharedHandleOwner() {
    if (acquire_fence_handle)
      CloseHandle(acquire_fence_handle);
    if (handle)
      CloseHandle(handle);
    av_frame_free(&frame);
  }
};

WindowsYuvMatrix matrix_from_av(int value) noexcept {
  // FFmpeg AVCOL_SPC_* numeric values are intentionally handled without
  // exposing FFmpeg enums in the public header.
  switch (value) {
  case 1:
    return WindowsYuvMatrix::bt709;
  case 9:
    return WindowsYuvMatrix::bt2020_ncl;
  default:
    return WindowsYuvMatrix::bt601;
  }
}

WindowsChromaSiting chroma_from_av(int value) noexcept {
  // AVCOL_CHROMA_LOC_LEFT=1, CENTER=2, TOPLEFT=3.
  switch (value) {
  case 2:
    return WindowsChromaSiting::center;
  case 3:
    return WindowsChromaSiting::top_left;
  default:
    return WindowsChromaSiting::left;
  }
}

DigitorResult hr_result(HRESULT hr) noexcept {
  if (hr == E_OUTOFMEMORY)
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  return FAILED(hr) ? DIGITOR_RESULT_BACKEND_UNAVAILABLE : DIGITOR_RESULT_OK;
}

bool make_shareable_slice_copy(ID3D11Texture2D *source,
                               std::uint32_t source_slice,
                               ComPtr<ID3D11Texture2D> &destination,
                               std::string &diagnostic) {
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
  const auto destination_desc = normalized_d3d11va_interop_desc(source_desc);
  ComPtr<ID3D11InfoQueue> info_queue;
  device.As(&info_queue);
  const UINT64 messages_before =
      info_queue ? info_queue->GetNumStoredMessagesAllowedByRetrievalFilter()
                 : 0;
  const HRESULT hr =
      device->CreateTexture2D(&destination_desc, nullptr, &destination);
  if (FAILED(hr) || !destination) {
    UINT support = 0;
    const HRESULT support_hr =
        device->CheckFormatSupport(source_desc.Format, &support);
    std::string debug_message;
    if (info_queue) {
      const UINT64 count =
          info_queue->GetNumStoredMessagesAllowedByRetrievalFilter();
      for (UINT64 index = messages_before; index < count; ++index) {
        SIZE_T size = 0;
        if (FAILED(info_queue->GetMessage(index, nullptr, &size)) || !size)
          continue;
        std::vector<unsigned char> storage(size);
        auto *message = reinterpret_cast<D3D11_MESSAGE *>(storage.data());
        if (SUCCEEDED(info_queue->GetMessage(index, message, &size)) &&
            message->pDescription) {
          if (!debug_message.empty())
            debug_message += " | ";
          debug_message.append(message->pDescription,
                               message->DescriptionByteLength);
        }
      }
    }
    diagnostic = format_d3d11_texture_creation_failure(
        hr, source_desc, destination_desc, device->GetFeatureLevel(),
        support_hr, support, debug_message);
    return false;
  }
  ComPtr<ID3D11DeviceContext> context;
  device->GetImmediateContext(&context);
  if (!context) {
    diagnostic = "D3D11VA device has no immediate context";
    destination.Reset();
    return false;
  }
  const UINT source_subresource =
      D3D11CalcSubresource(0, source_slice, source_desc.MipLevels);
  context->CopySubresourceRegion(destination.Get(), 0, 0, 0, 0, source,
                                 source_subresource, nullptr);
  return true;
}

bool create_nt_handle(ID3D11Texture2D *texture, HANDLE &handle,
                      std::string &diagnostic) {
  ComPtr<IDXGIResource1> resource;
  HRESULT hr = texture->QueryInterface(IID_PPV_ARGS(&resource));
  if (FAILED(hr) || !resource) {
    diagnostic = "D3D11 texture does not expose IDXGIResource1";
    return false;
  }
  hr = resource->CreateSharedHandle(
      nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
      &handle);
  if (FAILED(hr) || !handle) {
    diagnostic = "IDXGIResource1::CreateSharedHandle failed";
    return false;
  }
  return true;
}

bool create_acquire_fence(ID3D11Texture2D *texture, ComPtr<ID3D11Fence> &fence,
                          HANDLE &shared_handle, std::uint64_t &value,
                          std::string &diagnostic) {
  ComPtr<ID3D11Device> base_device;
  texture->GetDevice(&base_device);
  if (!base_device) {
    diagnostic = "D3D11VA texture has no owning device for synchronization";
    return false;
  }
  ComPtr<ID3D11Device5> device;
  if (FAILED(base_device.As(&device)) || !device) {
    diagnostic = "D3D11 device does not expose shared fence support";
    return false;
  }
  ComPtr<ID3D11DeviceContext> base_context;
  base_device->GetImmediateContext(&base_context);
  ComPtr<ID3D11DeviceContext4> context;
  if (!base_context || FAILED(base_context.As(&context)) || !context) {
    diagnostic = "D3D11 immediate context does not expose fence signaling";
    return false;
  }
  HRESULT hr =
      device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence));
  if (FAILED(hr) || !fence) {
    diagnostic = "ID3D11Device5::CreateFence failed";
    return false;
  }
  hr = fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &shared_handle);
  if (FAILED(hr) || !shared_handle) {
    diagnostic = "ID3D11Fence::CreateSharedHandle failed";
    fence.Reset();
    return false;
  }
  value = 1;
  hr = context->Signal(fence.Get(), value);
  if (FAILED(hr)) {
    CloseHandle(shared_handle);
    shared_handle = nullptr;
    fence.Reset();
    diagnostic = "ID3D11DeviceContext4::Signal failed";
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
    void *opaque_frame, std::int64_t timestamp_us, bool normalized_timestamp,
    FfmpegD3D11vaExtractionResult &out) noexcept {
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
  auto *frame = static_cast<AVFrame *>(opaque_frame);
  if (frame->format != AV_PIX_FMT_D3D11 || !frame->data[0]) {
    out.diagnostic = "AVFrame is not an AV_PIX_FMT_D3D11 surface";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  out.frame_is_d3d11 = true;

  try {
    auto owner = std::make_shared<SharedHandleOwner>();
    owner->frame = av_frame_alloc();
    if (!owner->frame)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    const int ref_result = av_frame_ref(owner->frame, frame);
    if (ref_result < 0) {
      out.diagnostic = "av_frame_ref failed for D3D11VA surface";
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    out.texture_retained = true;

    auto *texture = reinterpret_cast<ID3D11Texture2D *>(frame->data[0]);
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

    std::uint64_t acquire_value = 0;
    if (!create_acquire_fence(owner->texture.Get(), owner->acquire_fence,
                              owner->acquire_fence_handle, acquire_value,
                              out.diagnostic))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.acquire_sync_created = true;
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
    native.native_device =
        reinterpret_cast<std::uintptr_t>(frame->hw_frames_ctx);
    native.timestamp_us =
        normalized_timestamp ? timestamp_us : frame->best_effort_timestamp;
    native.acquire_sync.type = NativeMediaSyncType::d3d11_fence;
    native.acquire_sync.handle =
        reinterpret_cast<std::uintptr_t>(owner->acquire_fence_handle);
    native.acquire_sync.value = acquire_value;
    native.color.primaries = frame->color_primaries;
    native.color.transfer = frame->color_trc;
    native.color.matrix = frame->colorspace;
    native.color.full_range = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    native.color.chroma_location =
        static_cast<std::uint8_t>(frame->chroma_location);

    auto native_surface = std::make_shared<NativeMediaSurface>(
        native, std::static_pointer_cast<void>(owner));

    out.surface.format = windows_format(desc.Format);
    out.surface.width = static_cast<std::uint32_t>(frame->width);
    out.surface.height = static_cast<std::uint32_t>(frame->height);
    out.surface.array_slice = exported_slice;
    out.surface.shared_handle = reinterpret_cast<std::uintptr_t>(shared);
    out.surface.decoder_device =
        reinterpret_cast<std::uintptr_t>(frame->hw_frames_ctx);
    out.surface.timestamp_us = native.timestamp_us;
    out.surface.color.matrix = matrix_from_av(frame->colorspace);
    out.surface.color.chroma_siting = chroma_from_av(frame->chroma_location);
    out.surface.color.full_range = frame->color_range == AVCOL_RANGE_JPEG;
    out.surface.color.primaries = frame->color_primaries;
    out.surface.color.transfer = frame->color_trc;
    out.surface.lifetime = std::move(native_surface);
    out.diagnostic.clear();
    return DIGITOR_RESULT_OK;
  } catch (const std::bad_alloc &) {
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

DigitorResult
extract_ffmpeg_d3d11va_surface(void *opaque_frame,
                               FfmpegD3D11vaExtractionResult &out) noexcept {
  return extract_ffmpeg_d3d11va_surface_impl(opaque_frame, 0, false, out);
}

DigitorResult
extract_ffmpeg_d3d11va_surface(void *opaque_frame, std::int64_t timestamp_us,
                               FfmpegD3D11vaExtractionResult &out) noexcept {
  return extract_ffmpeg_d3d11va_surface_impl(opaque_frame, timestamp_us, true,
                                             out);
}

} // namespace digitor
