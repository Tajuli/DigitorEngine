#include "digitor/windows_d3d12_p010_converter.hpp"

#include <atomic>
#include <cstdio>
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

WindowsP010GpuConstants windows_p010_gpu_constants(
    const WindowsP010ConversionConfig& c) noexcept {
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
  o.flags=(c.full_range?1u:0u)|(c.preserve_superwhites?2u:0u)|
          (c.input_transfer_encoded?4u:0u);
  return o;
}

struct WindowsD3D12P010Converter::Impl {
  WindowsP010ConversionConfig config;
  mutable std::mutex mutex;
  WindowsP010ConverterTelemetry telemetry;
#ifdef _WIN32
  struct Slot {
    Microsoft::WRL::ComPtr<ID3D12Resource> d3d12;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> d3d11;
    std::atomic_bool in_use{false};
  };
  Microsoft::WRL::ComPtr<ID3D12Device> device12;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue12;
  Microsoft::WRL::ComPtr<ID3D11Device1> device11_1;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence12;
  HANDLE fence_event{};
  std::atomic_uint64_t sequence{1};
  std::vector<std::shared_ptr<Slot>> slots;
  ~Impl(){ if(fence_event) CloseHandle(fence_event); }
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
    auto fail_hr=[&i](const char* operation,HRESULT hr) noexcept {
      char message[224]{};
      std::snprintf(message,sizeof(message),"%s failed: HRESULT=0x%08lX",
                    operation,
                    static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
      {
        std::scoped_lock lock(i.mutex);
        i.telemetry.diagnostic=message;
      }
      std::fprintf(stderr,"[DigitorEngine] P010 converter %s\n",message);
      std::fflush(stderr);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    };
    auto fail_message=[&i](const char* message) noexcept {
      {
        std::scoped_lock lock(i.mutex);
        i.telemetry.diagnostic=message;
      }
      std::fprintf(stderr,"[DigitorEngine] P010 converter %s\n",message);
      std::fflush(stderr);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    };

    if(!i.config.d3d12_device||!i.config.command_queue||!i.config.d3d11_device||
       !i.config.width||!i.config.height||!i.config.pool_size||!i.config.gpu_dispatch)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    if(i.config.input_format!=DIGITOR_PIXEL_FORMAT_RGBA8_UNORM &&
       i.config.input_format!=DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT &&
       i.config.input_format!=DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT)
      return DIGITOR_RESULT_UNSUPPORTED;
    if((i.config.width&1u)||(i.config.height&1u)) return DIGITOR_RESULT_INVALID_ARGUMENT;

    i.device12=static_cast<ID3D12Device*>(i.config.d3d12_device);
    i.queue12=static_cast<ID3D12CommandQueue*>(i.config.command_queue);
    auto* raw11=static_cast<ID3D11Device*>(i.config.d3d11_device);
    HRESULT hr=raw11->QueryInterface(IID_PPV_ARGS(&i.device11_1));
    if(FAILED(hr)||!i.device11_1)
      return fail_hr("QueryInterface(ID3D11Device1)",hr);

    // Do not require D3D11.5 shared-fence interop here. Some otherwise-valid
    // hardware/driver combinations can open the shared P010 resource but fail
    // ID3D11Device5/OpenSharedFence. Pixel conversion still runs entirely on
    // D3D12; the CPU only waits for producer completion and never stages or
    // reads video pixels.
    hr=i.device12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&i.fence12));
    if(FAILED(hr)||!i.fence12)
      return fail_hr("ID3D12Device::CreateFence(P010 completion)",hr);
    i.fence_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!i.fence_event)
      return fail_message("CreateEventW(P010 completion) failed");

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
      hr=i.device12->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_SHARED,&desc,
          D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&slot->d3d12));
      if(FAILED(hr)||!slot->d3d12)
        return fail_hr("ID3D12Device::CreateCommittedResource(shared P010 UAV)",hr);
      HANDLE shared{};
      hr=i.device12->CreateSharedHandle(slot->d3d12.Get(),nullptr,GENERIC_ALL,nullptr,&shared);
      if(FAILED(hr)||!shared)
        return fail_hr("ID3D12Device::CreateSharedHandle(P010)",hr);
      const HRESULT opened=i.device11_1->OpenSharedResource1(shared,IID_PPV_ARGS(&slot->d3d11));
      CloseHandle(shared);
      if(FAILED(opened)||!slot->d3d11)
        return fail_hr("ID3D11Device1::OpenSharedResource1(P010)",opened);
      i.slots.push_back(std::move(slot));
    }
    std::scoped_lock lock(i.mutex);
    i.telemetry.diagnostic="shared P010 GPU surface pool initialized with D3D12 fence completion";
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
  if(!input.resource||input.format!=i.config.input_format||
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
    const auto constants=windows_p010_gpu_constants(i.config);
    {std::scoped_lock lock(i.mutex);++i.telemetry.submitted;}
    const auto result=i.config.gpu_dispatch(input.resource,slot->d3d12.Get(),constants,
                                            i.queue12.Get(),i.fence12.Get(),value);
    if(result!=DIGITOR_RESULT_OK){release_on_error();return result;}

    // The producer queue has signaled fence12 for this exact conversion. A
    // bounded CPU wait is used only for synchronization; video pixels remain
    // GPU-resident from the D3D12 shader through the shared D3D11 P010 surface.
    if(i.fence12->GetCompletedValue()<value){
      const HRESULT wait_hr=i.fence12->SetEventOnCompletion(value,i.fence_event);
      if(FAILED(wait_hr)){
        release_on_error();
        std::scoped_lock lock(i.mutex);++i.telemetry.synchronization_failures;
        i.telemetry.diagnostic="ID3D12Fence::SetEventOnCompletion(P010) failed";
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      const DWORD wait=WaitForSingleObject(i.fence_event,30'000);
      if(wait!=WAIT_OBJECT_0){
        release_on_error();
        std::scoped_lock lock(i.mutex);++i.telemetry.synchronization_failures;
        const HRESULT removed=i.device12->GetDeviceRemovedReason();
        if(FAILED(removed)){
          ++i.telemetry.device_lost_events;
          i.telemetry.diagnostic="D3D12 device was removed while waiting for P010 conversion";
        } else if(wait==WAIT_TIMEOUT){
          i.telemetry.diagnostic="timed out waiting for GPU P010 conversion completion";
        } else {
          i.telemetry.diagnostic="failed waiting for GPU P010 conversion completion";
        }
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
    }

    auto lifetime=std::shared_ptr<void>(slot.get(),[slot](void*){
      slot->in_use.store(false,std::memory_order_release);
    });
    output.resource=slot->d3d11.Get();
    output.width=i.config.width; output.height=i.config.height;
    output.timestamp_us=input.timestamp_us;
    output.frame_identity=input.frame_identity;
    output.lifetime=std::move(lifetime);
    {std::scoped_lock lock(i.mutex);++i.telemetry.completed;
      i.telemetry.diagnostic="GPU RGBA converted to shared P010; CPU used for fence wait only";}
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
