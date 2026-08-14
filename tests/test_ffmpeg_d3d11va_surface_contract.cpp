#include "digitor/ffmpeg_d3d11va_surface.hpp"
#include "media/ffmpeg_d3d11va_surface_internal.hpp"

#include <cassert>
#include <iostream>

#ifdef _WIN32
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#endif

#ifdef _WIN32
namespace {
using Microsoft::WRL::ComPtr;

void verify_descriptor(DXGI_FORMAT format) {
  D3D11_TEXTURE2D_DESC decoder{};
  decoder.Width = 1920;
  decoder.Height = 1152;
  decoder.MipLevels = 1;
  decoder.ArraySize = 20;
  decoder.Format = format;
  decoder.SampleDesc = {1, 0};
  decoder.Usage = D3D11_USAGE_DEFAULT;
  decoder.BindFlags = D3D11_BIND_DECODER;
  decoder.MiscFlags = 0;
  const auto normalized = digitor::normalized_d3d11va_interop_desc(decoder);
  assert(normalized.Width == decoder.Width &&
         normalized.Height == decoder.Height);
  assert(normalized.MipLevels == 1 && normalized.ArraySize == 1);
  assert(normalized.Format == format && normalized.SampleDesc.Count == 1);
  assert(normalized.Usage == D3D11_USAGE_DEFAULT);
  assert(normalized.BindFlags == 0);
  assert(normalized.CPUAccessFlags == 0);
  assert(normalized.MiscFlags == D3D11_RESOURCE_MISC_SHARED_NTHANDLE);

  const auto text = digitor::format_d3d11_texture_creation_failure(
      E_INVALIDARG, decoder, normalized, D3D_FEATURE_LEVEL_11_0, S_OK,
      D3D11_FORMAT_SUPPORT_TEXTURE2D,
      "test validation message resource=0x123456789ABCDEF0");
  assert(text.find("HRESULT=0x80070057") != std::string::npos);
  assert(text.find("E_INVALIDARG") != std::string::npos);
  assert(text.find("source={Width=1920") != std::string::npos);
  assert(text.find("destination={Width=1920") != std::string::npos);
  assert(text.find("test validation message") != std::string::npos);
  assert(text.find("123456789ABCDEF0") == std::string::npos);
}

void verify_gpu_interop(DXGI_FORMAT format) {
  ComPtr<ID3D11Device> device11;
  ComPtr<ID3D11DeviceContext> context11;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                               nullptr, 0, D3D11_SDK_VERSION, &device11,
                               nullptr, &context11))) {
    std::cout << "SKIP: no hardware D3D11 device\n";
    return;
  }
  UINT support = 0;
  if (FAILED(device11->CheckFormatSupport(format, &support)) ||
      !(support & D3D11_FORMAT_SUPPORT_TEXTURE2D)) {
    std::cout << "SKIP: planar format unsupported: " << format << '\n';
    return;
  }
  D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
  D3D11_FEATURE_DATA_D3D11_OPTIONS4 options4{};
  D3D11_FEATURE_DATA_D3D11_OPTIONS5 options5{};
  const HRESULT options_hr = device11->CheckFeatureSupport(
      D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options));
  const HRESULT options4_hr = device11->CheckFeatureSupport(
      D3D11_FEATURE_D3D11_OPTIONS4, &options4, sizeof(options4));
  const HRESULT options5_hr = device11->CheckFeatureSupport(
      D3D11_FEATURE_D3D11_OPTIONS5, &options5, sizeof(options5));
  std::cout << "D3D11 ExtendedResourceSharing="
            << (SUCCEEDED(options_hr) ? options.ExtendedResourceSharing : -1)
            << " ExtendedNV12SharedTextureSupported="
            << (SUCCEEDED(options4_hr)
                    ? options4.ExtendedNV12SharedTextureSupported
                    : -1)
            << " SharedResourceTier="
            << (SUCCEEDED(options5_hr)
                    ? static_cast<int>(options5.SharedResourceTier)
                    : -1)
            << '\n';
  D3D11_TEXTURE2D_DESC source_desc{};
  source_desc.Width = 1920;
  source_desc.Height = 1152;
  source_desc.MipLevels = 1;
  source_desc.ArraySize = 20;
  source_desc.Format = format;
  source_desc.SampleDesc = {1, 0};
  source_desc.Usage = D3D11_USAGE_DEFAULT;
  source_desc.BindFlags = D3D11_BIND_DECODER;
  ComPtr<ID3D11Texture2D> source;
  if (FAILED(device11->CreateTexture2D(&source_desc, nullptr, &source))) {
    std::cout << "SKIP: device cannot create source planar array\n";
    return;
  }
  const auto destination_desc =
      digitor::normalized_d3d11va_interop_desc(source_desc);
  auto shader_desc = destination_desc;
  shader_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  auto keyed_desc = destination_desc;
  keyed_desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
  auto keyed_shader_desc = keyed_desc;
  keyed_shader_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  auto legacy_desc = destination_desc;
  legacy_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
  ComPtr<ID3D11Texture2D> destination;
  const HRESULT bind0_hr =
      device11->CreateTexture2D(&destination_desc, nullptr, &destination);
  ComPtr<ID3D11Texture2D> shader_destination;
  const HRESULT shader_hr =
      device11->CreateTexture2D(&shader_desc, nullptr, &shader_destination);
  ComPtr<ID3D11Texture2D> keyed_destination;
  const HRESULT keyed_hr =
      device11->CreateTexture2D(&keyed_desc, nullptr, &keyed_destination);
  ComPtr<ID3D11Texture2D> keyed_shader_destination;
  const HRESULT keyed_shader_hr = device11->CreateTexture2D(
      &keyed_shader_desc, nullptr, &keyed_shader_destination);
  ComPtr<ID3D11Texture2D> legacy_destination;
  const HRESULT legacy_hr =
      device11->CreateTexture2D(&legacy_desc, nullptr, &legacy_destination);
  std::cout << "format=" << format << " A_nthandle_bind0_hresult=0x" << std::hex
            << static_cast<unsigned long>(bind0_hr)
            << " B_nthandle_shader_resource_hresult=0x"
            << static_cast<unsigned long>(shader_hr)
            << " C_nthandle_keyedmutex_bind0_hresult=0x"
            << static_cast<unsigned long>(keyed_hr)
            << " D_nthandle_keyedmutex_shader_resource_hresult=0x"
            << static_cast<unsigned long>(keyed_shader_hr)
            << " E_legacy_shared_bind0_hresult=0x"
            << static_cast<unsigned long>(legacy_hr) << std::dec << '\n';
  // The production contract intentionally remains case A pending results from
  // real Windows hardware. Matrix success alone must not select keyed mutex or
  // legacy sharing because the D3D12 consumer uses an NT handle and a fence.
  if (FAILED(bind0_hr)) {
    std::cout << "QUALIFICATION: production case A unavailable; matrix "
                 "results recorded, D3D12 import not attempted\n";
    return;
  }
  // Exercise a non-zero decoder slice and preserve the single-slice contract.
  context11->CopySubresourceRegion(
      destination.Get(), 0, 0, 0, 0, source.Get(),
      D3D11CalcSubresource(0, 2, source_desc.MipLevels), nullptr);

  ComPtr<IDXGIResource1> dxgi;
  assert(SUCCEEDED(destination.As(&dxgi)));
  HANDLE texture_handle = nullptr;
  assert(SUCCEEDED(dxgi->CreateSharedHandle(
      nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
      &texture_handle)));
  ComPtr<ID3D12Device> device12;
  assert(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                     IID_PPV_ARGS(&device12))));
  D3D12_FEATURE_DATA_D3D12_OPTIONS4 d3d12_options4{};
  const HRESULT d3d12_options4_hr = device12->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS4, &d3d12_options4, sizeof(d3d12_options4));
  std::cout << "D3D12 SharedResourceCompatibilityTier="
            << (SUCCEEDED(d3d12_options4_hr)
                    ? static_cast<int>(
                          d3d12_options4.SharedResourceCompatibilityTier)
                    : -1)
            << '\n';
  ComPtr<ID3D12Resource> imported;
  assert(SUCCEEDED(
      device12->OpenSharedHandle(texture_handle, IID_PPV_ARGS(&imported))));
  assert(imported->GetDesc().DepthOrArraySize == 1);
  assert(imported->GetDesc().Format == format);
  const DXGI_FORMAT y_format =
      format == DXGI_FORMAT_P010 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
  const DXGI_FORMAT uv_format = format == DXGI_FORMAT_P010
                                    ? DXGI_FORMAT_R16G16_UNORM
                                    : DXGI_FORMAT_R8G8_UNORM;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = 2;
  ComPtr<ID3D12DescriptorHeap> heap;
  assert(SUCCEEDED(
      device12->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap))));
  auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
  const auto increment = device12->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Texture2D.MipLevels = 1;
  srv.Format = y_format;
  srv.Texture2D.PlaneSlice = 0;
  device12->CreateShaderResourceView(imported.Get(), &srv, cpu);
  cpu.ptr += increment;
  srv.Format = uv_format;
  srv.Texture2D.PlaneSlice = 1;
  device12->CreateShaderResourceView(imported.Get(), &srv, cpu);
  std::cout << "opened_desc width=" << imported->GetDesc().Width
            << " height=" << imported->GetDesc().Height
            << " y_plane_srv=created uv_plane_srv=created\n";
  CloseHandle(texture_handle);

  ComPtr<ID3D11Device5> device11_5;
  ComPtr<ID3D11DeviceContext4> context11_4;
  if (SUCCEEDED(device11.As(&device11_5)) &&
      SUCCEEDED(context11.As(&context11_4))) {
    ComPtr<ID3D11Fence> fence11;
    assert(SUCCEEDED(device11_5->CreateFence(0, D3D11_FENCE_FLAG_SHARED,
                                             IID_PPV_ARGS(&fence11))));
    HANDLE fence_handle = nullptr;
    assert(SUCCEEDED(fence11->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr,
                                                 &fence_handle)));
    assert(SUCCEEDED(context11_4->Signal(fence11.Get(), 1)));
    ComPtr<ID3D12Fence> fence12;
    assert(SUCCEEDED(
        device12->OpenSharedHandle(fence_handle, IID_PPV_ARGS(&fence12))));
    CloseHandle(fence_handle);
  }
}
} // namespace
#endif

int main() {
  using namespace digitor;
  FfmpegD3D11vaExtractionResult out;
  const auto low_level = extract_ffmpeg_d3d11va_surface(nullptr, out);
  assert(low_level == DIGITOR_RESULT_INVALID_ARGUMENT ||
         low_level == DIGITOR_RESULT_UNSUPPORTED);
  assert(!out.acquire_sync_created);
  assert(!out.no_cpu_transfer);

  out = {};
  constexpr std::int64_t engine_timestamp_us = 123456;
  const auto production =
      extract_ffmpeg_d3d11va_surface(nullptr, engine_timestamp_us, out);
  assert(production == DIGITOR_RESULT_INVALID_ARGUMENT ||
         production == DIGITOR_RESULT_UNSUPPORTED);
  assert(!out.acquire_sync_created);
  assert(!out.no_cpu_transfer);
#ifdef _WIN32
  verify_descriptor(DXGI_FORMAT_NV12);
  verify_descriptor(DXGI_FORMAT_P010);
  verify_gpu_interop(DXGI_FORMAT_NV12);
  verify_gpu_interop(DXGI_FORMAT_P010);
#endif
  return 0;
}
