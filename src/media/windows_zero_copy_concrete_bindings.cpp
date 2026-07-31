#include "digitor/windows_zero_copy_concrete_bindings.hpp"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <windows.h>
#include <wrl/client.h>
#endif

namespace digitor {

struct WindowsD3D12LeaseRegistry::Impl {
  std::mutex mutex;
  std::unordered_map<std::uint64_t,WindowsD3D12ProducedFrame> frames;
};
WindowsD3D12LeaseRegistry::WindowsD3D12LeaseRegistry():impl_(std::make_shared<Impl>()){}
WindowsD3D12LeaseRegistry::~WindowsD3D12LeaseRegistry()=default;
DigitorResult WindowsD3D12LeaseRegistry::publish(WindowsD3D12ProducedFrame f) noexcept {
  if(!f.rgba16f_resource||!f.width||!f.height||!f.frame_identity||!f.lifetime)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  try{std::scoped_lock lock(impl_->mutex);impl_->frames[f.frame_identity]=std::move(f);return DIGITOR_RESULT_OK;}
  catch(...){return DIGITOR_RESULT_OUT_OF_MEMORY;}
}
void WindowsD3D12LeaseRegistry::retire(std::uint64_t id) noexcept {std::scoped_lock lock(impl_->mutex);impl_->frames.erase(id);}
void WindowsD3D12LeaseRegistry::clear() noexcept {std::scoped_lock lock(impl_->mutex);impl_->frames.clear();}
WindowsD3D12LeaseProvider WindowsD3D12LeaseRegistry::provider(){
  auto keep=impl_;
  return [keep](const ProcessedGpuFramePtr& frame,WindowsNativeConsumerKind kind,WindowsD3D12FrameLease& out) noexcept {
    out.reset();if(!frame)return DIGITOR_RESULT_INVALID_ARGUMENT;
    try{
      WindowsD3D12ProducedFrame value;
      {std::scoped_lock lock(keep->mutex);auto it=keep->frames.find(frame->identity());if(it==keep->frames.end())return DIGITOR_RESULT_NOT_INITIALIZED;value=it->second;}
      if(value.timestamp_us!=frame->metadata().timestamp||value.width!=frame->metadata().width||value.height!=frame->metadata().height)
        return DIGITOR_RESULT_INTERNAL_ERROR;
      auto lifetime=value.lifetime;
      out.resource=value.rgba16f_resource;out.producer_fence=value.producer_fence;out.producer_fence_value=value.fence_value;
      out.width=value.width;out.height=value.height;out.format=DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
      out.timestamp_us=value.timestamp_us;out.frame_identity=value.frame_identity;out.consumer=kind;
      out.release=[lifetime=std::move(lifetime)]() mutable {lifetime.reset();};
      return DIGITOR_RESULT_OK;
    }catch(...){return DIGITOR_RESULT_INTERNAL_ERROR;}
  };
}

struct WindowsD3D12SwapchainPresenter::Impl {
  WindowsD3D12SwapchainPresenterConfig config;
#ifdef _WIN32
  Microsoft::WRL::ComPtr<ID3D12Device> device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence;
  HANDLE event_handle{};std::uint64_t fence_value{};
  ~Impl(){if(event_handle)CloseHandle(event_handle);}
#endif
};
WindowsD3D12SwapchainPresenter::WindowsD3D12SwapchainPresenter(WindowsD3D12SwapchainPresenterConfig c):impl_(std::make_shared<Impl>()){
  impl_->config=c;
#ifdef _WIN32
  if(c.device)impl_->device=static_cast<ID3D12Device*>(c.device);
  if(c.command_queue)impl_->queue=static_cast<ID3D12CommandQueue*>(c.command_queue);
  if(c.swapchain)impl_->swapchain=static_cast<IDXGISwapChain3*>(c.swapchain);
  if(impl_->device&&impl_->queue&&impl_->swapchain){
    impl_->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&impl_->allocator));
    if(impl_->allocator&&SUCCEEDED(impl_->device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,impl_->allocator.Get(),nullptr,IID_PPV_ARGS(&impl_->list))))impl_->list->Close();
    impl_->device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&impl_->fence));
    impl_->event_handle=CreateEvent(nullptr,FALSE,FALSE,nullptr);
  }
#endif
}
WindowsD3D12SwapchainPresenter::~WindowsD3D12SwapchainPresenter()=default;
DigitorResult WindowsD3D12SwapchainPresenter::present(const WindowsD3D12FrameLease& lease) noexcept {
#ifndef _WIN32
  (void)lease;return DIGITOR_RESULT_UNSUPPORTED;
#else
  if(!lease||!impl_->device||!impl_->queue||!impl_->swapchain||!impl_->allocator||!impl_->list||!impl_->fence||!impl_->event_handle)return DIGITOR_RESULT_NOT_INITIALIZED;
  auto* source=static_cast<ID3D12Resource*>(lease.resource);
  if(lease.producer_fence&&impl_->queue->Wait(static_cast<ID3D12Fence*>(lease.producer_fence),lease.producer_fence_value)!=S_OK)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  Microsoft::WRL::ComPtr<ID3D12Resource> back;
  if(FAILED(impl_->swapchain->GetBuffer(impl_->swapchain->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&back))))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  const auto sd=source->GetDesc(),bd=back->GetDesc();
  if(sd.Width!=bd.Width||sd.Height!=bd.Height||sd.Format!=bd.Format||sd.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return DIGITOR_RESULT_UNSUPPORTED;
  if(FAILED(impl_->allocator->Reset())||FAILED(impl_->list->Reset(impl_->allocator.Get(),nullptr)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  D3D12_RESOURCE_BARRIER b[2]{};
  b[0].Type=b[1].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b[0].Transition={back.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST};
  b[1].Transition={source,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
  impl_->list->ResourceBarrier(2,b);impl_->list->CopyResource(back.Get(),source);
  std::swap(b[0].Transition.StateBefore,b[0].Transition.StateAfter);std::swap(b[1].Transition.StateBefore,b[1].Transition.StateAfter);impl_->list->ResourceBarrier(2,b);
  if(FAILED(impl_->list->Close()))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;ID3D12CommandList* lists[]{impl_->list.Get()};impl_->queue->ExecuteCommandLists(1,lists);
  const auto value=++impl_->fence_value;if(FAILED(impl_->queue->Signal(impl_->fence.Get(),value)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  if(FAILED(impl_->swapchain->Present(1,impl_->config.allow_tearing?DXGI_PRESENT_ALLOW_TEARING:0)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  return DIGITOR_RESULT_OK;
#endif
}
WindowsD3D12PresentCallback WindowsD3D12SwapchainPresenter::callback(){auto keep=impl_;return [keep](const WindowsD3D12FrameLease& l)noexcept{WindowsD3D12SwapchainPresenter p({});p.impl_=keep;return p.present(l);};}

struct WindowsMediaFoundationHardwareEncoder::Impl {
  WindowsMediaFoundationEncoderConfig config;WindowsRgba16fToP010 convert;std::atomic_uint64_t frames{};
#ifdef _WIN32
  Microsoft::WRL::ComPtr<IMFSinkWriter> writer;DWORD stream{};bool started{};
#endif
};
WindowsMediaFoundationHardwareEncoder::WindowsMediaFoundationHardwareEncoder(WindowsMediaFoundationEncoderConfig c,WindowsRgba16fToP010 fn):impl_(std::make_shared<Impl>()){impl_->config=std::move(c);impl_->convert=std::move(fn);}
WindowsMediaFoundationHardwareEncoder::~WindowsMediaFoundationHardwareEncoder(){(void)flush();}
DigitorResult WindowsMediaFoundationHardwareEncoder::initialize() noexcept {
#ifndef _WIN32
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  if(!impl_->convert||impl_->config.output_path.empty()||!impl_->config.width||!impl_->config.height)return DIGITOR_RESULT_INVALID_ARGUMENT;
  if(FAILED(MFStartup(MF_VERSION)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  int n=MultiByteToWideChar(CP_UTF8,0,impl_->config.output_path.c_str(),-1,nullptr,0);if(n<=0)return DIGITOR_RESULT_INVALID_ARGUMENT;std::wstring path(n,0);MultiByteToWideChar(CP_UTF8,0,impl_->config.output_path.c_str(),-1,path.data(),n);
  Microsoft::WRL::ComPtr<IMFAttributes> attrs;MFCreateAttributes(&attrs,3);attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE);attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING,TRUE);
  if(impl_->config.dxgi_device_manager)attrs->SetUnknown(MF_SINK_WRITER_D3D_MANAGER,static_cast<IUnknown*>(impl_->config.dxgi_device_manager));
  if(FAILED(MFCreateSinkWriterFromURL(path.c_str(),nullptr,attrs.Get(),&impl_->writer)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  Microsoft::WRL::ComPtr<IMFMediaType> out,in;MFCreateMediaType(&out);out->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);out->SetGUID(MF_MT_SUBTYPE,impl_->config.hevc?MFVideoFormat_HEVC:MFVideoFormat_H264);out->SetUINT32(MF_MT_AVG_BITRATE,impl_->config.bitrate);MFSetAttributeSize(out.Get(),MF_MT_FRAME_SIZE,impl_->config.width,impl_->config.height);MFSetAttributeRatio(out.Get(),MF_MT_FRAME_RATE,impl_->config.fps_num,impl_->config.fps_den);MFSetAttributeRatio(out.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);
  if(FAILED(impl_->writer->AddStream(out.Get(),&impl_->stream)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  MFCreateMediaType(&in);in->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);in->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_P010);MFSetAttributeSize(in.Get(),MF_MT_FRAME_SIZE,impl_->config.width,impl_->config.height);MFSetAttributeRatio(in.Get(),MF_MT_FRAME_RATE,impl_->config.fps_num,impl_->config.fps_den);MFSetAttributeRatio(in.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);in->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
  if(FAILED(impl_->writer->SetInputMediaType(impl_->stream,in.Get(),nullptr))||FAILED(impl_->writer->BeginWriting()))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  impl_->started=true;return DIGITOR_RESULT_OK;
#endif
}
DigitorResult WindowsMediaFoundationHardwareEncoder::submit(const WindowsD3D12FrameLease& lease) noexcept {
#ifndef _WIN32
  (void)lease;return DIGITOR_RESULT_UNSUPPORTED;
#else
  if(!impl_->started||!impl_->writer)return DIGITOR_RESULT_NOT_INITIALIZED;WindowsP010EncoderSurface s;auto r=impl_->convert(lease,s);if(r!=DIGITOR_RESULT_OK)return r;
  if(!s.resource||s.width!=impl_->config.width||s.height!=impl_->config.height||s.timestamp_us!=lease.timestamp_us||s.frame_identity!=lease.frame_identity)return DIGITOR_RESULT_INTERNAL_ERROR;
  Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;auto* texture=static_cast<ID3D11Texture2D*>(s.resource);if(FAILED(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D),texture,0,FALSE,&buffer)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  Microsoft::WRL::ComPtr<IMFSample> sample;MFCreateSample(&sample);sample->AddBuffer(buffer.Get());sample->SetSampleTime(s.timestamp_us*10);const auto duration=(10000000LL*impl_->config.fps_den)/impl_->config.fps_num;sample->SetSampleDuration(duration);
  if(FAILED(impl_->writer->WriteSample(impl_->stream,sample.Get())))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;++impl_->frames;return DIGITOR_RESULT_OK;
#endif
}
WindowsHardwareEncoderConsumer::SubmitCallback WindowsMediaFoundationHardwareEncoder::callback(){auto keep=impl_;return [keep](const WindowsD3D12FrameLease& l)noexcept{WindowsMediaFoundationHardwareEncoder e({},{});e.impl_=keep;return e.submit(l);};}
DigitorResult WindowsMediaFoundationHardwareEncoder::flush() noexcept {
#ifdef _WIN32
  if(impl_&&impl_->writer&&impl_->started){auto hr=impl_->writer->Finalize();impl_->started=false;impl_->writer.Reset();MFShutdown();return SUCCEEDED(hr)?DIGITOR_RESULT_OK:DIGITOR_RESULT_BACKEND_UNAVAILABLE;}
#endif
  return DIGITOR_RESULT_OK;
}
std::uint64_t WindowsMediaFoundationHardwareEncoder::submitted_frames() const noexcept{return impl_?impl_->frames.load():0;}

} // namespace digitor
