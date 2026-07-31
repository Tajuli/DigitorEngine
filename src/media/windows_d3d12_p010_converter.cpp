#include "digitor/windows_d3d12_p010_converter.hpp"

#include <atomic>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>
#endif

namespace digitor {
namespace {
WindowsP010GpuConstants constants_for(const WindowsP010ConversionConfig& c) {
  WindowsP010GpuConstants o{};
  if (c.matrix == WindowsOutputMatrix::bt2020_ncl) {
    const float m[12]{0.2627f,0.6780f,0.0593f,0.0f,
                     -0.13963f,-0.36037f,0.5f,0.0f,
                     0.5f,-0.45979f,-0.04021f,0.0f};
    for (int i=0;i<12;++i) o.rgb_to_yuv[i]=m[i];
  } else {
    const float m[12]{0.2126f,0.7152f,0.0722f,0.0f,
                     -0.114572f,-0.385428f,0.5f,0.0f,
                     0.5f,-0.454153f,-0.045847f,0.0f};
    for (int i=0;i<12;++i) o.rgb_to_yuv[i]=m[i];
  }
  if (c.full_range) {
    o.y_offset=0.0f; o.y_scale=1023.0f;
    o.uv_offset=512.0f; o.uv_scale=1023.0f;
  } else {
    o.y_offset=64.0f; o.y_scale=876.0f;
    o.uv_offset=512.0f; o.uv_scale=896.0f;
  }
  o.mastering_peak_nits=c.mastering_peak_nits;
  o.width=c.width; o.height=c.height;
  o.transfer=static_cast<std::uint32_t>(c.transfer);
  o.flags=(c.full_range?1u:0u)|(c.preserve_superwhites?2u:0u);
  return o;
}
}

struct WindowsD3D12P010Converter::Impl {
  WindowsP010ConversionConfig config;
  mutable std::mutex mutex;
  WindowsP010ConverterTelemetry telemetry;
#ifdef _WIN32
  using Microsoft::WRL::ComPtr;
  struct Slot {
    ComPtr<ID3D12Resource> d3d12;
    ComPtr<ID3D11Texture2D> d3d11;
    std::atomic_bool in_use{false};
  };
  ComPtr<ID3D12Device> device12;
  ComPtr<ID3D12CommandQueue> queue12;
  ComPtr<ID3D11Device1> device11_1;
  ComPtr<ID3D11Device5> device11_5;
  ComPtr<ID3D11DeviceContext4> context11_4;
  ComPtr<ID3D12Fence> fence12;
  ComPtr<ID3D11Fence> fence11;
  HANDLE fence_handle{};
  std::atomic_uint64_t sequence{1};
  std::vector<std::shared_ptr<Slot>> slots;
  ~Impl(){ if(fence_handle) CloseHandle(fence_handle); }
#endif
};

WindowsD3D12P010Converter::WindowsD3D12P010Converter(WindowsP010ConversionConfig c)
    : impl_(std::make_shared<Impl>()) { impl_->config=std::move(c); }
WindowsD3D12P010Converter::~WindowsD3D12P010Converter()=default;

DigitorResult WindowsD3D12P010Converter::initialize() noexcept {
#ifndef _WIN32
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  try {
    auto& i=*impl_;
    if(!i.config.d3d12_device||!i.config.command_queue||!i.config.d3d11_device||
       !i.config.width||!i.config.height||!i.config.pool_size||!i.config.gpu_dispatch)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    if((i.config.width&1u)||(i.config.height&1u)) return DIGITOR_RESULT_INVALID_ARGUMENT;
    i.device12=static_cast<ID3D12Device*>(i.config.d3d12_device);
    i.queue12=static_cast<ID3D12CommandQueue*>(i.config.command_queue);
    auto* raw11=static_cast<ID3D11Device*>(i.config.d3d11_device);
    if(FAILED(raw11->QueryInterface(IID_PPV_ARGS(&i.device11_1)))||
       FAILED(raw11->QueryInterface(IID_PPV_ARGS(&i.device11_5))))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> base_context;
    raw11->GetImmediateContext(&base_context);
    if(FAILED(base_context.As(&i.context11_4))) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    if(FAILED(i.device12->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&i.fence12))))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    if(FAILED(i.device12->CreateSharedHandle(i.fence12.Get(),nullptr,GENERIC_ALL,nullptr,&i.fence_handle)))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    if(FAILED(i.device11_5->OpenSharedFence(i.fence_handle,IID_PPV_ARGS(&i.fence11))))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    D3D12_HEAP_PROPERTIES hp{}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=i.config.width; desc.Height=i.config.height;
    desc.DepthOrArraySize=1; desc.MipLevels=1;
    desc.Format=DXGI_FORMAT_P010; desc.SampleDesc.Count=1;
    desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    i.slots.clear(); i.slots.reserve(i.config.pool_size);
    for(std::uint32_t n=0;n<i.config.pool_size;++n){
      auto slot=std::make_shared<Impl::Slot>();
      if(FAILED(i.device12->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_SHARED,&desc,
          D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&slot->d3d12))))
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      HANDLE shared{};
      if(FAILED(i.device12->CreateSharedHandle(slot->d3d12.Get(),nullptr,GENERIC_ALL,nullptr,&shared)))
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      const HRESULT opened=i.device11_1->OpenSharedResource1(shared,IID_PPV_ARGS(&slot->d3d11));
      CloseHandle(shared);
      if(FAILED(opened)) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      i.slots.push_back(std::move(slot));
    }
    std::scoped_lock lock(i.mutex);
    i.telemetry.diagnostic="shared P010 GPU surface pool initialized";
    return DIGITOR_RESULT_OK;
  } catch(const std::bad_alloc&) { return DIGITOR_RESULT_OUT_OF_MEMORY; }
    catch(...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
#endif
}

DigitorResult WindowsD3D12P010Converter::convert(
    const WindowsD3D12FrameLease& input, WindowsP010EncoderSurface& output) noexcept {
  output={};
#ifndef _WIN32
  (void)input; return DIGITOR_RESULT_UNSUPPORTED;
#else
  auto& i=*impl_;
  if(!input.resource||input.format!=DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT||
     input.width!=i.config.width||input.height!=i.config.height)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  try {
    std::shared_ptr<Impl::Slot> slot;
    for(auto& candidate:i.slots){
      bool expected=false;
      if(candidate->in_use.compare_exchange_strong(expected,true,std::memory_order_acq_rel)){
        slot=candidate; break;
      }
    }
    if(!slot){std::scoped_lock lock(i.mutex);++i.telemetry.pool_exhaustions;
      i.telemetry.diagnostic="P010 surface pool exhausted";return DIGITOR_RESULT_RESOURCE_IN_USE;}
    const auto release_on_error=[&](){slot->in_use.store(false,std::memory_order_release);};
    const auto value=i.sequence.fetch_add(1,std::memory_order_relaxed);
    const auto constants=constants_for(i.config);
    {std::scoped_lock lock(i.mutex);++i.telemetry.submitted;}
    const auto result=i.config.gpu_dispatch(input.resource,slot->d3d12.Get(),constants,
                                            i.queue12.Get(),i.fence12.Get(),value);
    if(result!=DIGITOR_RESULT_OK){release_on_error();return result;}
    i.context11_4->Wait(i.fence11.Get(),value);
    auto lifetime=std::shared_ptr<void>(slot.get(),[slot](void*){
      slot->in_use.store(false,std::memory_order_release);
    });
    output.resource=slot->d3d11.Get();
    output.width=i.config.width; output.height=i.config.height;
    output.timestamp_us=input.timestamp_us;
    output.frame_identity=input.frame_identity;
    output.lifetime=std::move(lifetime);
    {std::scoped_lock lock(i.mutex);++i.telemetry.completed;
      i.telemetry.diagnostic="RGBA16F converted to shared P010 on GPU";}
    return DIGITOR_RESULT_OK;
  } catch(const std::bad_alloc&) { return DIGITOR_RESULT_OUT_OF_MEMORY; }
    catch(...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
#endif
}

WindowsRgba16fToP010 WindowsD3D12P010Converter::callback(){
  auto keep=impl_;
  return [keep](const WindowsD3D12FrameLease& in,WindowsP010EncoderSurface& out) noexcept {
    WindowsD3D12P010Converter c(keep->config); c.impl_=keep; return c.convert(in,out);
  };
}
WindowsP010ConverterTelemetry WindowsD3D12P010Converter::telemetry() const {
  std::scoped_lock lock(impl_->mutex); return impl_->telemetry;
}
bool WindowsD3D12P010Converter::gpu_only() const noexcept {
  std::scoped_lock lock(impl_->mutex); return impl_->telemetry.cpu_copies==0;
}

} // namespace digitor
