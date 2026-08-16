#include "digitor/windows_d3d12_p010_converter.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <new>
#include <thread>
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
    Microsoft::WRL::ComPtr<ID3D11Texture2D> p010;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> output_view;
    std::atomic_bool in_use{false};
  };
  Microsoft::WRL::ComPtr<ID3D12Device> device12;
  Microsoft::WRL::ComPtr<ID3D11Device> device11;
  Microsoft::WRL::ComPtr<ID3D11Device1> device11_1;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context11;
  Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_device;
  Microsoft::WRL::ComPtr<ID3D11VideoContext> video_context;
  Microsoft::WRL::ComPtr<ID3D11VideoContext1> video_context1;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessor> processor;
  Microsoft::WRL::ComPtr<ID3D11Query> completion_query;
  std::atomic_uint32_t next_slot{0};
  std::vector<std::shared_ptr<Slot>> slots;
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
      char message[256]{};
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

    if(!i.config.d3d12_device||!i.config.d3d11_device||
       !i.config.width||!i.config.height||!i.config.pool_size)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    if(i.config.input_format!=DIGITOR_PIXEL_FORMAT_RGBA8_UNORM)
      return DIGITOR_RESULT_UNSUPPORTED;
    if((i.config.width&1u)||(i.config.height&1u))
      return DIGITOR_RESULT_INVALID_ARGUMENT;

    i.device12=static_cast<ID3D12Device*>(i.config.d3d12_device);
    i.device11=static_cast<ID3D11Device*>(i.config.d3d11_device);

    HRESULT hr=i.device11.As(&i.device11_1);
    if(FAILED(hr)||!i.device11_1)
      return fail_hr("QueryInterface(ID3D11Device1)",hr);

    i.device11->GetImmediateContext(&i.context11);
    if(!i.context11)
      return fail_message("ID3D11Device::GetImmediateContext returned null");

    hr=i.device11.As(&i.video_device);
    if(FAILED(hr)||!i.video_device)
      return fail_hr("QueryInterface(ID3D11VideoDevice)",hr);
    hr=i.context11.As(&i.video_context);
    if(FAILED(hr)||!i.video_context)
      return fail_hr("QueryInterface(ID3D11VideoContext)",hr);
    (void)i.video_context.As(&i.video_context1);

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputFrameRate={30,1};
    content.InputWidth=i.config.width;
    content.InputHeight=i.config.height;
    content.OutputFrameRate={30,1};
    content.OutputWidth=i.config.width;
    content.OutputHeight=i.config.height;
    content.Usage=D3D11_VIDEO_USAGE_OPTIMAL_QUALITY;
    hr=i.video_device->CreateVideoProcessorEnumerator(&content,&i.enumerator);
    if(FAILED(hr)||!i.enumerator)
      return fail_hr("ID3D11VideoDevice::CreateVideoProcessorEnumerator",hr);

    UINT rgba_support=0;
    hr=i.enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_R8G8B8A8_UNORM,
                                               &rgba_support);
    if(FAILED(hr))
      return fail_hr("CheckVideoProcessorFormat(RGBA8)",hr);
    if(!(rgba_support&D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT))
      return fail_message("hardware video processor does not accept RGBA8 input");

    UINT p010_support=0;
    hr=i.enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_P010,&p010_support);
    if(FAILED(hr))
      return fail_hr("CheckVideoProcessorFormat(P010)",hr);
    if(!(p010_support&D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT))
      return fail_message("hardware video processor does not expose P010 output");

    hr=i.video_device->CreateVideoProcessor(i.enumerator.Get(),0,&i.processor);
    if(FAILED(hr)||!i.processor)
      return fail_hr("ID3D11VideoDevice::CreateVideoProcessor",hr);

    // The renderer output transform is an encoded RGBA8 surface. Use the
    // video processor's explicit color-space conversion rather than treating
    // P010 as a D3D11 unordered-access texture. On Windows 10 the v1 context
    // carries the complete BT.709/BT.2020/PQ contract. Older contexts retain
    // an SDR BT.709 fallback only.
    if(i.video_context1) {
      DXGI_COLOR_SPACE_TYPE input_space=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
      DXGI_COLOR_SPACE_TYPE output_space=i.config.full_range
          ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709
          : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
      if(i.config.matrix==WindowsOutputMatrix::bt2020_ncl) {
        if(i.config.transfer==WindowsOutputTransfer::pq) {
          input_space=DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
          output_space=DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020;
        } else {
          input_space=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020;
          output_space=i.config.full_range
              ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020
              : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020;
        }
      }
      i.video_context1->VideoProcessorSetStreamColorSpace1(
          i.processor.Get(),0,input_space);
      i.video_context1->VideoProcessorSetOutputColorSpace1(
          i.processor.Get(),output_space);
    } else {
      if(i.config.matrix!=WindowsOutputMatrix::bt709 ||
         i.config.transfer!=WindowsOutputTransfer::gamma24) {
        return fail_message(
            "Windows video processor lacks ID3D11VideoContext1 for HDR/BT.2020 export");
      }
      D3D11_VIDEO_PROCESSOR_COLOR_SPACE input_space{};
      input_space.Usage=1;
      input_space.RGB_Range=0;
      D3D11_VIDEO_PROCESSOR_COLOR_SPACE output_space{};
      output_space.Usage=1;
      output_space.YCbCr_Matrix=1;
      output_space.Nominal_Range=i.config.full_range
          ? D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255
          : D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
      i.video_context->VideoProcessorSetStreamColorSpace(
          i.processor.Get(),0,&input_space);
      i.video_context->VideoProcessorSetOutputColorSpace(
          i.processor.Get(),&output_space);
    }

    D3D11_QUERY_DESC query_desc{};
    query_desc.Query=D3D11_QUERY_EVENT;
    hr=i.device11->CreateQuery(&query_desc,&i.completion_query);
    if(FAILED(hr)||!i.completion_query)
      return fail_hr("ID3D11Device::CreateQuery(video processor completion)",hr);

    // Video-processor output resources require RENDER_TARGET. VIDEO_ENCODER is
    // explicitly allowed with it. Do not add UNORDERED_ACCESS: real hardware
    // rejected the old P010 UAV+encoder combination with E_INVALIDARG.
    D3D11_TEXTURE2D_DESC output_desc{};
    output_desc.Width=i.config.width;
    output_desc.Height=i.config.height;
    output_desc.MipLevels=1;
    output_desc.ArraySize=1;
    output_desc.Format=DXGI_FORMAT_P010;
    output_desc.SampleDesc.Count=1;
    output_desc.Usage=D3D11_USAGE_DEFAULT;
    output_desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_VIDEO_ENCODER;
    output_desc.CPUAccessFlags=0;
    output_desc.MiscFlags=0;

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_view_desc{};
    output_view_desc.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
    output_view_desc.Texture2D.MipSlice=0;

    i.slots.clear();
    i.slots.reserve(i.config.pool_size);
    for(std::uint32_t n=0;n<i.config.pool_size;++n) {
      auto slot=std::make_shared<Impl::Slot>();
      hr=i.device11->CreateTexture2D(&output_desc,nullptr,&slot->p010);
      if(FAILED(hr)||!slot->p010)
        return fail_hr("ID3D11Device::CreateTexture2D(P010 video-processor output)",hr);
      hr=i.video_device->CreateVideoProcessorOutputView(
          slot->p010.Get(),i.enumerator.Get(),&output_view_desc,
          &slot->output_view);
      if(FAILED(hr)||!slot->output_view)
        return fail_hr("ID3D11VideoDevice::CreateVideoProcessorOutputView(P010)",hr);
      i.slots.push_back(std::move(slot));
    }

    std::scoped_lock lock(i.mutex);
    i.telemetry.diagnostic=
        "D3D11 video-processor P010 encoder pool initialized; no CPU pixel staging";
    return DIGITOR_RESULT_OK;
  } catch(const std::bad_alloc&) {
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch(...) {
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
#endif
}

DigitorResult WindowsD3D12P010Converter::convert(
    const WindowsD3D12FrameLease& input,
    WindowsP010EncoderSurface& output) noexcept {
  output={};
#ifndef _WIN32
  (void)input;
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  auto& i=*impl_;
  if(!input.resource||input.format!=DIGITOR_PIXEL_FORMAT_RGBA8_UNORM||
     input.width!=i.config.width||input.height!=i.config.height||
     !i.device12||!i.device11_1||!i.video_device||!i.video_context||
     !i.enumerator||!i.processor||!i.completion_query||i.slots.empty())
    return DIGITOR_RESULT_INVALID_ARGUMENT;

  auto set_diagnostic=[&i](const char* message) {
    std::scoped_lock lock(i.mutex);
    i.telemetry.diagnostic=message;
  };
  auto set_hr_diagnostic=[&i](const char* operation,HRESULT hr) {
    char message[256]{};
    std::snprintf(message,sizeof(message),"%s failed: HRESULT=0x%08lX",
                  operation,
                  static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    {
      std::scoped_lock lock(i.mutex);
      i.telemetry.diagnostic=message;
    }
    std::fprintf(stderr,"[DigitorEngine] P010 converter %s\n",message);
    std::fflush(stderr);
  };

  try {
    std::shared_ptr<Impl::Slot> slot;
    const auto first=i.next_slot.fetch_add(1,std::memory_order_relaxed);
    for(std::size_t offset=0;offset<i.slots.size();++offset) {
      auto& candidate=i.slots[(first+offset)%i.slots.size()];
      bool expected=false;
      if(candidate->in_use.compare_exchange_strong(
             expected,true,std::memory_order_acq_rel)) {
        slot=candidate;
        break;
      }
    }
    if(!slot) {
      std::scoped_lock lock(i.mutex);
      ++i.telemetry.pool_exhaustions;
      i.telemetry.diagnostic="P010 video-processor surface pool exhausted";
      return DIGITOR_RESULT_RESOURCE_IN_USE;
    }
    const auto release_on_error=[&slot]() {
      slot->in_use.store(false,std::memory_order_release);
    };

    // The final renderer output transform is already a shareable RGBA8 D3D12
    // resource. Re-export its NT handle and open that exact allocation on the
    // same-adapter D3D11 device. No intermediate CPU image is created.
    auto* source12=static_cast<ID3D12Resource*>(input.resource);
    HANDLE shared_source{};
    HRESULT hr=i.device12->CreateSharedHandle(
        source12,nullptr,GENERIC_ALL,nullptr,&shared_source);
    if(FAILED(hr)||!shared_source) {
      release_on_error();
      set_hr_diagnostic("ID3D12Device::CreateSharedHandle(final RGBA8)",hr);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> source11;
    hr=i.device11_1->OpenSharedResource1(
        shared_source,IID_PPV_ARGS(&source11));
    CloseHandle(shared_source);
    if(FAILED(hr)||!source11) {
      release_on_error();
      set_hr_diagnostic("ID3D11Device1::OpenSharedResource1(final RGBA8)",hr);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    D3D11_TEXTURE2D_DESC source_desc{};
    source11->GetDesc(&source_desc);
    if(source_desc.Width!=i.config.width||
       source_desc.Height!=i.config.height||
       source_desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||
       source_desc.SampleDesc.Count!=1) {
      release_on_error();
      set_diagnostic("shared final RGBA8 resource metadata is incompatible with D3D11 video processing");
      return DIGITOR_RESULT_UNSUPPORTED;
    }

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_view_desc{};
    input_view_desc.FourCC=0;
    input_view_desc.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
    input_view_desc.Texture2D.MipSlice=0;
    input_view_desc.Texture2D.ArraySlice=0;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> input_view;
    hr=i.video_device->CreateVideoProcessorInputView(
        source11.Get(),i.enumerator.Get(),&input_view_desc,&input_view);
    if(FAILED(hr)||!input_view) {
      release_on_error();
      set_hr_diagnostic("ID3D11VideoDevice::CreateVideoProcessorInputView(RGBA8)",hr);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable=TRUE;
    stream.OutputIndex=0;
    stream.InputFrameOrField=0;
    stream.PastFrames=0;
    stream.FutureFrames=0;
    stream.pInputSurface=input_view.Get();

    {
      std::scoped_lock lock(i.mutex);
      ++i.telemetry.submitted;
    }
    hr=i.video_context->VideoProcessorBlt(
        i.processor.Get(),slot->output_view.Get(),0,1,&stream);
    if(FAILED(hr)) {
      release_on_error();
      set_hr_diagnostic("ID3D11VideoContext::VideoProcessorBlt(RGBA8->P010)",hr);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    // Completion wait is synchronization only. No RGBA/P010 bytes are mapped,
    // read, or staged through CPU memory.
    i.context11->End(i.completion_query.Get());
    i.context11->Flush();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    for(;;) {
      const HRESULT done=i.context11->GetData(
          i.completion_query.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
      if(done==S_OK)
        break;
      if(FAILED(done)) {
        release_on_error();
        {
          std::scoped_lock lock(i.mutex);
          ++i.telemetry.synchronization_failures;
        }
        set_hr_diagnostic("ID3D11DeviceContext::GetData(video processor completion)",done);
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      if(std::chrono::steady_clock::now()>=deadline) {
        release_on_error();
        std::scoped_lock lock(i.mutex);
        ++i.telemetry.synchronization_failures;
        i.telemetry.diagnostic="timed out waiting for D3D11 video-processor P010 completion";
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      std::this_thread::yield();
    }

    auto lifetime=std::shared_ptr<void>(slot.get(),[slot](void*) {
      slot->in_use.store(false,std::memory_order_release);
    });
    output.resource=slot->p010.Get();
    output.width=i.config.width;
    output.height=i.config.height;
    output.timestamp_us=input.timestamp_us;
    output.frame_identity=input.frame_identity;
    output.lifetime=std::move(lifetime);
    {
      std::scoped_lock lock(i.mutex);
      ++i.telemetry.completed;
      i.telemetry.diagnostic=
          "final RGBA8 converted to encoder P010 by D3D11 video processor on GPU";
    }
    return DIGITOR_RESULT_OK;
  } catch(const std::bad_alloc&) {
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch(...) {
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
#endif
}

WindowsRgba16fToP010 WindowsD3D12P010Converter::callback(){
  auto keep=impl_;
  return [keep](const WindowsD3D12FrameLease& in,
                WindowsP010EncoderSurface& out) noexcept {
    WindowsD3D12P010Converter c(keep->config);
    c.impl_=keep;
    return c.convert(in,out);
  };
}
WindowsP010ConverterTelemetry WindowsD3D12P010Converter::telemetry() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->telemetry;
}
bool WindowsD3D12P010Converter::gpu_only() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return impl_->telemetry.cpu_copies==0;
}

} // namespace digitor
