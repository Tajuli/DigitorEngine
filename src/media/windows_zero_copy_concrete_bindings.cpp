#include "digitor/windows_zero_copy_concrete_bindings.hpp"
#include "digitor/flutter_production_host_adapter.hpp"
#include "digitor/windows_d3d12_p010_converter.hpp"
#include "digitor/windows_d3d12_p010_dispatch.hpp"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
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

#if defined(_WIN32)
namespace {
using Microsoft::WRL::ComPtr;

struct DefaultWindowsExportState {
  ProductionTextureDescriptorBuilder descriptor_builder;
  std::shared_ptr<const ExportRenderSnapshot> snapshot;
  std::mutex mutex;
  HardwareEncodeConfig config;
  std::filesystem::path final_path;
  std::filesystem::path staged_path;
  bool opened{};
  bool initialized{};
  bool cancelled{};
  bool finalized{};
  bool no_cpu_pixels{true};
  bool synchronization_waited{};
  bool native_resource_registered{};
  std::uint64_t frames{};
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<ID3D12Device> device12;
  ComPtr<ID3D12CommandQueue> queue12;
  ComPtr<ID3D11Device> device11;
  ComPtr<IMFDXGIDeviceManager> device_manager;
  UINT device_manager_token{};
  std::unique_ptr<WindowsD3D12P010Dispatch> dispatch;
  std::unique_ptr<WindowsD3D12P010Converter> converter;
  std::unique_ptr<WindowsMediaFoundationHardwareEncoder> encoder;
};

std::filesystem::path staged_output_path(const std::filesystem::path& final_path) {
  const auto stem=final_path.stem().string()+".digitor-partial";
  return final_path.parent_path()/(stem+final_path.extension().string());
}

DigitorResult open_same_adapter_devices(DefaultWindowsExportState& s,HANDLE shared,
                                        std::uint32_t width,std::uint32_t height,
                                        std::string& diagnostic) {
  ComPtr<IDXGIFactory6> factory;
  if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    diagnostic="DXGI factory creation failed for production export";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  for(UINT index=0;;++index) {
    ComPtr<IDXGIAdapter1> adapter;
    if(factory->EnumAdapters1(index,&adapter)==DXGI_ERROR_NOT_FOUND) break;
    DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);
    if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) continue;
    ComPtr<ID3D12Device> device;
    if(FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)))) continue;
    ComPtr<ID3D12Resource> probe;
    if(FAILED(device->OpenSharedHandle(shared,IID_PPV_ARGS(&probe)))) continue;
    const auto resource_desc=probe->GetDesc();
    if(resource_desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||
       resource_desc.Width!=width||resource_desc.Height!=height||
       resource_desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM) continue;
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    ComPtr<ID3D12CommandQueue> queue;
    if(FAILED(device->CreateCommandQueue(&queue_desc,IID_PPV_ARGS(&queue)))) continue;
    UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext> context11;
    D3D_FEATURE_LEVEL selected{};
    if(FAILED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,flags,
        levels,static_cast<UINT>(std::size(levels)),D3D11_SDK_VERSION,&device11,&selected,&context11))) continue;
    ComPtr<IMFDXGIDeviceManager> manager;UINT token{};
    if(FAILED(MFCreateDXGIDeviceManager(&token,&manager))||
       FAILED(manager->ResetDevice(device11.Get(),token))) continue;
    s.adapter=std::move(adapter);s.device12=std::move(device);s.queue12=std::move(queue);
    s.device11=std::move(device11);s.device_manager=std::move(manager);s.device_manager_token=token;
    diagnostic.clear();return DIGITOR_RESULT_OK;
  }
  diagnostic="no hardware adapter could open the final graded D3D12 shared resource";
  return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
}

DigitorResult initialize_default_encoder(DefaultWindowsExportState& s,
                                         const DigitorNativeGpuTextureDescriptor& descriptor,
                                         std::string& diagnostic) {
  if(s.initialized) return DIGITOR_RESULT_OK;
  auto handle=reinterpret_cast<HANDLE>(descriptor.native_handle);
  auto result=open_same_adapter_devices(s,handle,descriptor.width,descriptor.height,diagnostic);
  if(result!=DIGITOR_RESULT_OK) return result;

  WindowsD3D12P010DispatchConfig dispatch_config{};
  dispatch_config.device=s.device12.Get();
  dispatch_config.input_format=DIGITOR_PIXEL_FORMAT_RGBA8_UNORM;
  dispatch_config.source_starts_shader_readable=false;
  s.dispatch=std::make_unique<WindowsD3D12P010Dispatch>(std::move(dispatch_config));
  result=s.dispatch->initialize();
  if(result!=DIGITOR_RESULT_OK){diagnostic="embedded D3D12 RGBA-to-P010 dispatch initialization failed";return result;}

  WindowsP010ConversionConfig conversion{};
  conversion.d3d12_device=s.device12.Get();conversion.command_queue=s.queue12.Get();
  conversion.d3d11_device=s.device11.Get();conversion.width=descriptor.width;conversion.height=descriptor.height;
  conversion.input_format=DIGITOR_PIXEL_FORMAT_RGBA8_UNORM;
  conversion.input_transfer_encoded=true;
  conversion.matrix=s.snapshot->data().hdr?WindowsOutputMatrix::bt2020_ncl:WindowsOutputMatrix::bt709;
  conversion.transfer=s.snapshot->data().hdr?WindowsOutputTransfer::pq:WindowsOutputTransfer::gamma24;
  conversion.mastering_peak_nits=s.snapshot->data().hdr?1000.0f:100.0f;
  conversion.gpu_dispatch=s.dispatch->callback();
  s.converter=std::make_unique<WindowsD3D12P010Converter>(std::move(conversion));
  result=s.converter->initialize();
  if(result!=DIGITOR_RESULT_OK){diagnostic="shared D3D12/D3D11 P010 converter initialization failed";return result;}

  WindowsMediaFoundationEncoderConfig encoder_config{};
  encoder_config.dxgi_device_manager=s.device_manager.Get();
  encoder_config.output_path=s.staged_path.string();
  encoder_config.width=descriptor.width;encoder_config.height=descriptor.height;
  encoder_config.fps_num=static_cast<std::uint32_t>(s.config.profile.fps_num);
  encoder_config.fps_den=static_cast<std::uint32_t>(s.config.profile.fps_den);
  encoder_config.bitrate=static_cast<std::uint32_t>(s.config.profile.video_bitrate);
  encoder_config.hevc=s.config.profile.codec==ExportCodec::hevc;
  encoder_config.main10=s.config.profile.ten_bit;
  s.encoder=std::make_unique<WindowsMediaFoundationHardwareEncoder>(
      std::move(encoder_config),s.converter->callback());
  result=s.encoder->initialize();
  if(result!=DIGITOR_RESULT_OK){diagnostic="Media Foundation hardware encoder initialization failed";return result;}
  s.initialized=true;diagnostic.clear();return DIGITOR_RESULT_OK;
}
} // namespace
#endif

void install_windows_default_export_factory(
    FlutterProductionHostAdapterInputs& inputs) noexcept {
#if !defined(_WIN32)
  (void)inputs;
#else
  try {
    if(inputs.encoder_factory || !inputs.texture_descriptor_builder ||
       inputs.preview_capabilities.backend!=DIGITOR_NATIVE_TEXTURE_BACKEND_D3D12 ||
       inputs.preview_capabilities.handle_type!=DIGITOR_NATIVE_TEXTURE_HANDLE_DXGI_SHARED_HANDLE ||
       !inputs.preview_capabilities.native_gpu_preview_available) return;
    const auto descriptor_builder=inputs.texture_descriptor_builder;
    inputs.encoder_backend=EncoderBackend::quick_sync;
    inputs.encoder_factory=[descriptor_builder](std::shared_ptr<const ExportRenderSnapshot> snapshot) {
      ProductionEncoderFactoryResult result{};
      if(!snapshot){result.diagnostic="missing immutable export snapshot";return result;}
      const auto& data=snapshot->data();
      if(data.renderer_backend!=DIGITOR_RENDERER_D3D12){result.diagnostic="default Windows encoder requires D3D12 final frames";return result;}
      if(data.profile.codec!=ExportCodec::h264 && data.profile.codec!=ExportCodec::hevc){result.diagnostic="default Windows encoder currently supports H.264 and HEVC";return result;}
      if(data.output_path.empty()){result.diagnostic="Windows export output path is empty";return result;}
      auto state=std::make_shared<DefaultWindowsExportState>();
      state->descriptor_builder=descriptor_builder;state->snapshot=std::move(snapshot);
      result.callbacks.open=[state](const HardwareEncodeConfig& config,std::string& diagnostic) noexcept {
        try {
          std::scoped_lock lock(state->mutex);
          if(state->opened){diagnostic="Windows production encoder already opened";return DIGITOR_RESULT_RESOURCE_IN_USE;}
          if(config.backend==EncoderBackend::software||!config.require_hardware||!config.require_zero_copy){diagnostic="Windows production export requires hardware zero-copy mode";return DIGITOR_RESULT_UNSUPPORTED;}
          state->config=config;state->final_path=config.output_path;state->staged_path=staged_output_path(state->final_path);
          std::error_code ec;std::filesystem::create_directories(state->final_path.parent_path(),ec);ec.clear();std::filesystem::remove(state->staged_path,ec);
          state->opened=true;state->cancelled=false;diagnostic.clear();return DIGITOR_RESULT_OK;
        } catch(...) {diagnostic="failed to open default Windows production encoder";return DIGITOR_RESULT_INTERNAL_ERROR;}
      };
      result.callbacks.submit_gpu_frame=[state](const HardwareEncodeFrame& input,std::string& diagnostic) noexcept {
        try {
          std::scoped_lock lock(state->mutex);
          if(!state->opened||state->cancelled||!input.frame){diagnostic="Windows production encoder is not active";return DIGITOR_RESULT_NOT_INITIALIZED;}
          const auto& m=input.frame->metadata();const auto& d=state->snapshot->data();
          if(input.frame->backend()!=DIGITOR_RENDERER_D3D12||!input.frame->ready()||!input.frame->context_live()||
             m.width!=d.width||m.height!=d.height||m.color_metadata!=d.color_metadata||
             (m.format!=DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT&&m.format!=DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT)){
            diagnostic="final graded GPU frame differs from the frozen Windows export contract";return DIGITOR_RESULT_INVALID_ARGUMENT;
          }
          DigitorNativeGpuTextureDescriptor descriptor{};descriptor.struct_size=sizeof(descriptor);descriptor.api_version=DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION;
          const auto generation=input.frame->identity()?input.frame->identity():1;
          auto r=state->descriptor_builder(input.frame,generation,descriptor,diagnostic);
          if(r!=DIGITOR_RESULT_OK)return r;
          if(descriptor.backend!=DIGITOR_NATIVE_TEXTURE_BACKEND_D3D12||
             descriptor.handle_type!=DIGITOR_NATIVE_TEXTURE_HANDLE_DXGI_SHARED_HANDLE||
             descriptor.pixel_format!=DIGITOR_PIXEL_FORMAT_RGBA8_UNORM||!descriptor.native_handle){
            diagnostic="Windows output-transform descriptor is not a shared D3D12 RGBA8 resource";return DIGITOR_RESULT_UNSUPPORTED;
          }
          r=initialize_default_encoder(*state,descriptor,diagnostic);if(r!=DIGITOR_RESULT_OK)return r;
          ComPtr<ID3D12Resource> shared;
          if(FAILED(state->device12->OpenSharedHandle(reinterpret_cast<HANDLE>(descriptor.native_handle),IID_PPV_ARGS(&shared)))){
            diagnostic="hardware encoder could not open the final output-transform resource";return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
          }
          WindowsD3D12FrameLease lease;
          lease.resource=shared.Get();lease.width=descriptor.width;lease.height=descriptor.height;
          lease.format=DIGITOR_PIXEL_FORMAT_RGBA8_UNORM;lease.timestamp_us=input.pts_us;
          lease.frame_identity=input.frame->identity();lease.consumer=WindowsNativeConsumerKind::hardware_encoder;
          lease.release=[shared=std::move(shared),frame=input.frame]() mutable {shared.Reset();(void)frame;};
          r=state->encoder->submit(lease);if(r!=DIGITOR_RESULT_OK){diagnostic="Media Foundation rejected the final graded GPU frame";return r;}
          ++state->frames;state->synchronization_waited=true;state->native_resource_registered=true;diagnostic.clear();return DIGITOR_RESULT_OK;
        } catch(...) {diagnostic="unexpected Windows final-frame encode failure";return DIGITOR_RESULT_INTERNAL_ERROR;}
      };
      result.callbacks.drain=[state](std::string& diagnostic) noexcept {
        std::scoped_lock lock(state->mutex);if(!state->opened||state->cancelled){diagnostic="Windows production encoder cannot drain";return DIGITOR_RESULT_NOT_INITIALIZED;}diagnostic.clear();return DIGITOR_RESULT_OK;
      };
      result.callbacks.finalize_atomic=[state](std::string& diagnostic) noexcept {
        try {
          std::scoped_lock lock(state->mutex);
          if(!state->opened||state->cancelled||!state->encoder||state->frames==0){diagnostic="Windows production encoder has no completed video frames";return DIGITOR_RESULT_NOT_INITIALIZED;}
          auto r=state->encoder->flush();if(r!=DIGITOR_RESULT_OK){diagnostic="Media Foundation export finalization failed";return r;}
          state->encoder.reset();std::error_code ec;std::filesystem::remove(state->final_path,ec);ec.clear();std::filesystem::rename(state->staged_path,state->final_path,ec);
          if(ec){diagnostic="failed to atomically publish Windows export output";return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}
          state->finalized=true;diagnostic.clear();return DIGITOR_RESULT_OK;
        } catch(...) {diagnostic="unexpected Windows export finalization failure";return DIGITOR_RESULT_INTERNAL_ERROR;}
      };
      result.callbacks.cancel=[state]() noexcept {
        std::scoped_lock lock(state->mutex);state->cancelled=true;if(state->encoder){(void)state->encoder->flush();state->encoder.reset();}std::error_code ec;std::filesystem::remove(state->staged_path,ec);
      };
      result.zero_copy_qualified=[state]() {
        std::scoped_lock lock(state->mutex);return state->finalized&&state->frames>0&&state->no_cpu_pixels&&state->synchronization_waited&&state->native_resource_registered;
      };
      result.windows_vulkan_interop_qualified=false;result.diagnostic.clear();return result;
    };
  } catch(...) {
    inputs.encoder_factory={};inputs.encoder_backend=EncoderBackend::software;
  }
#endif
}

} // namespace digitor
