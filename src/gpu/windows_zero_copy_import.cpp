#include "digitor/windows_zero_copy_import.hpp"

#include <new>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>
#endif

namespace digitor {

bool validate_windows_zero_copy_surface(const WindowsZeroCopySurface& s,
                                        std::string* diagnostic) noexcept {
  auto fail = [&](const char* text) {
    if (diagnostic) *diagnostic = text;
    return false;
  };
  if (s.struct_size < sizeof(WindowsZeroCopySurface) || s.api_version != 1)
    return fail("unsupported Windows zero-copy descriptor version");
  if (!s.width || !s.height || (s.width & 1u) || (s.height & 1u))
    return fail("NV12/P010 dimensions must be non-zero and even");
  if (!s.shared_handle) return fail("DXGI shared handle is required");
  if (!s.lifetime) return fail("decoder surface lifetime is required");
  if (s.format != WindowsZeroCopyFormat::nv12 &&
      s.format != WindowsZeroCopyFormat::p010)
    return fail("only NV12 and P010 are supported");
  if (s.color.matrix != WindowsYuvMatrix::bt601 &&
      s.color.matrix != WindowsYuvMatrix::bt709 &&
      s.color.matrix != WindowsYuvMatrix::bt2020_ncl)
    return fail("unsupported YUV matrix");
  if (diagnostic) diagnostic->clear();
  return true;
}

struct WindowsD3D12ZeroCopyImporter::Impl {
#ifdef _WIN32
  Microsoft::WRL::ComPtr<ID3D12Device> device;
#else
  void* device{};
#endif
  WindowsD3D12ConvertCallback converter;
  DigitorPixelFormat expected_output_format{DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT};
};

WindowsD3D12ZeroCopyImporter::WindowsD3D12ZeroCopyImporter(
    void* d3d12_device, WindowsD3D12ConvertCallback converter,
    DigitorPixelFormat expected_output_format)
    : impl_(std::make_unique<Impl>()) {
  if (!d3d12_device || !converter)
    throw std::invalid_argument("D3D12 device and converter are required");
  if (expected_output_format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT &&
      expected_output_format != DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT)
    throw std::invalid_argument("D3D12 import output must be RGBA16F or RGBA32F");
#ifdef _WIN32
  impl_->device = static_cast<ID3D12Device*>(d3d12_device);
#else
  impl_->device = d3d12_device;
#endif
  impl_->converter = std::move(converter);
  impl_->expected_output_format = expected_output_format;
}

WindowsD3D12ZeroCopyImporter::~WindowsD3D12ZeroCopyImporter() = default;

DigitorResult WindowsD3D12ZeroCopyImporter::import(
    const WindowsZeroCopySurface& surface, ProcessedGpuFramePtr& out,
    WindowsZeroCopyQualification* q) noexcept {
  out.reset();
  WindowsZeroCopyQualification local;
  auto& qualification = q ? *q : local;
  qualification = {};
  qualification.descriptor_valid =
      validate_windows_zero_copy_surface(surface, &qualification.diagnostic);
  qualification.format_supported = qualification.descriptor_valid;
  qualification.decoder_lifetime_retained = static_cast<bool>(surface.lifetime);
  qualification.no_cpu_readback = true;
  qualification.per_pixel_contract_preserved = qualification.descriptor_valid;
  if (!qualification.descriptor_valid)
    return DIGITOR_RESULT_INVALID_ARGUMENT;

#ifndef _WIN32
  qualification.diagnostic = "Windows D3D12 import is unavailable on this host";
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  try {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    const auto handle = reinterpret_cast<HANDLE>(surface.shared_handle);
    const HRESULT hr = impl_->device->OpenSharedHandle(
        handle, IID_PPV_ARGS(resource.ReleaseAndGetAddressOf()));
    if (FAILED(hr) || !resource) {
      qualification.diagnostic = "ID3D12Device::OpenSharedHandle failed";
      return hr == E_OUTOFMEMORY ? DIGITOR_RESULT_OUT_OF_MEMORY
                                 : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    qualification.shared_resource_opened = true;

    const auto desc = resource->GetDesc();
    const DXGI_FORMAT expected = surface.format == WindowsZeroCopyFormat::nv12
                                     ? DXGI_FORMAT_NV12
                                     : DXGI_FORMAT_P010;
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        desc.Format != expected || desc.Width < surface.width ||
        desc.Height < surface.height ||
        surface.array_slice >= desc.DepthOrArraySize) {
      qualification.diagnostic =
          "shared resource does not match the decoder surface contract";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    qualification.plane_views_valid = true;

    const auto result = impl_->converter(resource.Get(), surface, out);
    if (result != DIGITOR_RESULT_OK) {
      out.reset();
      qualification.diagnostic = "GPU YUV conversion callback failed";
      return result;
    }
    if (!out || out->backend() != DIGITOR_RENDERER_D3D12 ||
        out->metadata().width != surface.width ||
        out->metadata().height != surface.height ||
        out->metadata().format != impl_->expected_output_format ||
        out->metadata().timestamp != surface.timestamp_us) {
      out.reset();
      qualification.diagnostic =
          "converter returned a frame that violates the requested floating-point contract";
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
    qualification.gpu_conversion_submitted = true;
    qualification.rgba16f_output =
        impl_->expected_output_format == DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
    qualification.diagnostic.clear();
    return DIGITOR_RESULT_OK;
  } catch (const std::bad_alloc&) {
    out.reset();
    qualification.diagnostic = "out of memory during D3D12 shared import";
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    out.reset();
    qualification.diagnostic = "unexpected D3D12 shared import failure";
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
#endif
}

} // namespace digitor
