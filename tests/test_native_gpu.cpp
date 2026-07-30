#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <string_view>
#include <string>
#include <vector>

#include "gpu/gpu_backend.hpp"
#include "core/numeric_utils.hpp"
#include "digitor/renderer.hpp"

namespace {
using Pixel = std::array<std::uint8_t, 4>;

std::vector<std::uint8_t> solid(std::uint32_t width, std::uint32_t height, Pixel color) {
    std::vector<std::uint8_t> pixels(std::size_t(width) * height * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4)
        std::copy(color.begin(), color.end(), pixels.begin() + static_cast<std::ptrdiff_t>(i));
    return pixels;
}

std::vector<std::uint8_t> pattern(std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint8_t> pixels(std::size_t(width) * height * 4);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto offset = (std::size_t(y) * width + x) * 4;
            pixels[offset] = static_cast<std::uint8_t>((x * 17 + y * 3) & 0xff);
            pixels[offset + 1] = static_cast<std::uint8_t>((x * 5 + y * 53) & 0xff);
            pixels[offset + 2] = static_cast<std::uint8_t>((x * 101 + y * 7) & 0xff);
            pixels[offset + 3] = static_cast<std::uint8_t>(128 + ((x + y) & 0x7f));
        }
    return pixels;
}

bool compare(std::string_view backend, std::string_view operation, std::uint32_t width,
             std::uint32_t height, const std::vector<std::uint8_t>& expected,
             const std::vector<std::uint8_t>& actual) {
    std::size_t mismatches = 0;
    std::size_t first = 0;
    const auto pixels = std::min(expected.size(), actual.size()) / 4;
    for (std::size_t i = 0; i < pixels; ++i) {
        const auto* expected_pixel = expected.data() + i * 4;
        const auto* actual_pixel = actual.data() + i * 4;
        if (!std::equal(expected_pixel, expected_pixel + 4, actual_pixel)) {
            if (mismatches++ == 0) first = i;
        }
    }
    const auto expected_pixels = (expected.size() + 3) / 4;
    const auto actual_pixels = (actual.size() + 3) / 4;
    if (expected_pixels != actual_pixels) {
        if (mismatches == 0) first = pixels;
        mismatches += expected_pixels > actual_pixels ? expected_pixels - actual_pixels
                                                      : actual_pixels - expected_pixels;
    }
    if (mismatches == 0) return true;
    const auto x = width ? first % width : 0;
    const auto y = width ? first / width : 0;
    auto print_pixel = [](const std::vector<std::uint8_t>& bytes, std::size_t index) {
        if ((index + 1) * 4 > bytes.size()) { std::cerr << "<missing>"; return; }
        std::cerr << '(' << unsigned(bytes[index * 4]) << ',' << unsigned(bytes[index * 4 + 1])
                  << ',' << unsigned(bytes[index * 4 + 2]) << ','
                  << unsigned(bytes[index * 4 + 3]) << ')';
    };
    std::cerr << "PIXEL MISMATCH backend=" << backend << " operation=" << operation
              << " dimensions=" << width << 'x' << height
              << " expected_bytes=" << expected.size() << " actual_bytes=" << actual.size()
              << " first_coordinate=(" << x << ',' << y << ") expected=";
    print_pixel(expected, first);
    std::cerr << " actual=";
    print_pixel(actual, first);
    std::cerr << " mismatching_pixels=" << mismatches << '\n';
    return false;
}

bool qualify_primary_wheels(digitor::IRenderBackend&backend,std::string_view name){
  using D=digitor::PrimaryWheelsDescriptor;struct Case{const char*name;D p;};D lift;lift.lift={.08f,-.03f,.02f};D lm;lm.lift_master=.05f;D gamma;gamma.gamma={.8f,1.2f,1.5f};D gm;gm.gamma_master=1.3f;D gain;gain.gain={1.1f,.8f,1.4f};D gam;gam.gain_master=.85f;D offset;offset.offset={-.04f,.03f,.08f};D om;om.offset_master=-.02f;D all=lift;all.gamma=gamma.gamma;all.gain=gain.gain;all.offset=offset.offset;all.lift_master=.01f;all.gamma_master=1.1f;all.gain_master=.9f;all.offset_master=.02f;
  const std::array cases{Case{"identity",{}},Case{"lift-rgb",lift},Case{"lift-master",lm},Case{"gamma-rgb",gamma},Case{"gamma-master",gm},Case{"gain-rgb",gain},Case{"gain-master",gam},Case{"offset-rgb",offset},Case{"offset-master",om},Case{"combined",all}};
  constexpr uint32_t width=7,height=5;std::vector<digitor::Color>input(size_t(width)*height);uint32_t state=0x12345678u;for(size_t n=0;n<input.size();++n){state=state*1664525u+1013904223u;auto f=[&](uint32_t shift){return (float((state>>shift)&255)/127.5f)-.5f;};input[n]={f(0),f(8),f(16),.15f+float(n%7)/10};}input[0]={0,0,0,.3f};input[1]={1,0,0,.4f};input[2]={0,1,0,.5f};input[3]={0,0,1,.6f};input[4]={-.75f,-.1f,-2.f,.7f};input[5]={1.5f,4.f,12.f,.8f};input[6]={.25f,.25f,.25f,.9f};
  bool passed=true;for(const auto&test:cases){auto p=digitor::PrimaryWheelsParameters::create(test.p);std::vector<digitor::Color>expected(input.size()),actual(input.size());digitor::apply_primary_wheels_reference(input,expected,*p);digitor::ProcessedGpuFramePtr frame;auto result=backend.process_primary_wheels_gpu(input,width,height,101,*p,frame);auto present=frame?backend.present_gpu_frame(frame):DIGITOR_RESULT_INTERNAL_ERROR;const auto preview_prov=backend.execution_provenance();auto read=frame?backend.validation_readback_primary_wheels(frame,actual):DIGITOR_RESULT_INTERNAL_ERROR;const auto validation_prov=backend.execution_provenance();double max_abs=0,max_rel=0,sum=0;size_t worst=0,first=SIZE_MAX,failures=0;for(size_t n=0;n<actual.size();++n)for(int c=0;c<4;c++){const float*e=&expected[n].r,*a=&actual[n].r;double error=std::abs(double(e[c])-a[c]),rel=error/std::max(1e-6,std::abs(double(e[c])));if(error>max_abs){max_abs=error;worst=n;}max_rel=std::max(max_rel,rel);sum+=error*error;if(error>2e-5&&rel>2e-5){if(first==SIZE_MAX)first=n;++failures;}}double rms=std::sqrt(sum/(actual.size()*4)),psnr=rms==0?INFINITY:20*std::log10(1/rms);digitor::VideoFrame ev{.width=width,.height=height,.pixels=expected},av{.width=width,.height=height,.pixels=actual};double ssim=digitor::calculate_ssim(ev,av);auto print=[](digitor::Color c){std::cerr<<'('<<c.r<<','<<c.g<<','<<c.b<<','<<c.a<<')';};std::cerr<<"PRIMARY_WHEELS_METRICS backend="<<name<<" device=\""<<backend.info().device_name<<"\" case="<<test.name<<" max_absolute_error="<<max_abs<<" max_relative_error="<<max_rel<<" rms="<<rms<<" psnr="<<psnr<<" ssim="<<ssim<<" failing_pixels="<<failures<<" first_failing_coordinate="<<(first==SIZE_MAX?-1:int(first%width))<<','<<(first==SIZE_MAX?-1:int(first/width))<<" worst_coordinate="<<worst%width<<','<<worst/width<<" expected=";print(expected[worst]);std::cerr<<" actual=";print(actual[worst]);std::cerr<<" cpu_delta="<<preview_prov.cpu_primary_wheels_invocations<<" fallback="<<preview_prov.primary_wheels_fallback_invocations<<" normal_readback="<<preview_prov.normal_preview_readback_count<<'\n';passed&=result==DIGITOR_RESULT_OK&&present==DIGITOR_RESULT_OK&&read==DIGITOR_RESULT_OK&&frame&&frame->identity()!=0&&preview_prov.preview_source==digitor::PreviewSource::gpu&&preview_prov.direct_preview_consumed&&preview_prov.cpu_primary_wheels_invocations==0&&preview_prov.primary_wheels_fallback_invocations==0&&preview_prov.normal_preview_readback_count==0&&validation_prov.validation_readback_completed&&validation_prov.cpu_primary_wheels_invocations==0&&validation_prov.primary_wheels_fallback_invocations==0&&validation_prov.normal_preview_readback_count==0&&failures==0&&rms<=5e-6&&ssim>=.99999;}
  digitor::set_gpu_failure_point(digitor::GpuFailurePoint::None);auto p=digitor::PrimaryWheelsParameters::create();digitor::ProcessedGpuFramePtr preview;passed&=backend.process_primary_wheels_gpu(input,width,height,103,*p,preview)==DIGITOR_RESULT_OK&&preview;passed&=preview&&backend.present_gpu_frame(preview)==DIGITOR_RESULT_OK;digitor::set_gpu_failure_point(digitor::GpuFailurePoint::None);return passed;
}

bool qualify_composition(digitor::IRenderBackend& backend,std::string_view name){
  constexpr std::uint32_t width=7,height=5;std::vector<digitor::Color>input(std::size_t(width)*height);for(std::size_t n=0;n<input.size();++n)input[n]={float(int(n%9)-3)/4.f,float(int(n%7)-2)/3.f,float(int(n%11)-4)/5.f,.2f+float(n%5)/7.f};
  digitor::PrimaryWheelsDescriptor wd;wd.lift={.04f,-.02f,.01f};wd.gamma={.9f,1.2f,.8f};wd.gain={1.1f,.95f,1.05f};wd.offset_master=.015f;auto wheels=digitor::PrimaryWheelsParameters::create(wd);digitor::RgbCurvesParameters cd;cd.master.points={{0,0},{.3f,.2f},{.7f,.82f},{1,1}};cd.red.points={{0,0},{.5f,.58f},{1,1}};auto curves=digitor::CompiledRgbCurves::compile(cd);bool passed=true;
  for(bool wheels_first:{true,false}){std::vector<digitor::Color>middle(input.size()),expected(input.size()),actual(input.size());if(wheels_first){digitor::apply_primary_wheels_reference(input,middle,*wheels);curves->apply(middle,expected);}else{curves->apply(input,middle);digitor::apply_primary_wheels_reference(middle,expected,*wheels);}const auto pw_before=digitor::primary_wheels_reference_count(),curve_before=digitor::cpu_curve_reference_count();digitor::ProcessedGpuFramePtr first,final;auto first_result=wheels_first?backend.process_primary_wheels_gpu(input,width,height,201,*wheels,first):backend.process_curves_gpu(input,width,height,201,*curves,first);auto source=backend.gpu_source(first);auto second_result=first_result==DIGITOR_RESULT_OK?(wheels_first?backend.process_curves_gpu(source,202,*curves,final):backend.process_primary_wheels_gpu(source,202,*wheels,final)):first_result;auto read=final?backend.validation_readback_primary_wheels(final,actual):DIGITOR_RESULT_INTERNAL_ERROR;double max_abs=0,max_rel=0,squares=0;std::size_t failures=0,first_failure=SIZE_MAX,worst=0;for(std::size_t n=0;n<actual.size();++n)for(int c=0;c<4;c++){auto e=(&expected[n].r)[c],a=(&actual[n].r)[c];double error=std::abs(double(e)-a),relative=error/std::max(1e-6,std::abs(double(e)));if(error>max_abs){max_abs=error;worst=n;}max_rel=std::max(max_rel,relative);squares+=error*error;if(error>2e-5&&relative>2e-5){if(first_failure==SIZE_MAX)first_failure=n;++failures;}}double rms=std::sqrt(squares/(actual.size()*4)),psnr=rms==0?INFINITY:20*std::log10(1/rms);digitor::VideoFrame ev{.width=width,.height=height,.pixels=expected},av{.width=width,.height=height,.pixels=actual};double ssim=digitor::calculate_ssim(ev,av);const auto&prov=backend.execution_provenance();auto print=[](digitor::Color c){std::cerr<<'('<<c.r<<','<<c.g<<','<<c.b<<','<<c.a<<')';};std::cerr<<"COMPOSITION_METRICS backend="<<name<<" device=\""<<backend.info().device_name<<"\" order="<<(wheels_first?"primary-then-curves":"curves-then-primary")<<" dimensions="<<width<<'x'<<height<<" max_absolute_error="<<max_abs<<" max_relative_error="<<max_rel<<" rms="<<rms<<" psnr="<<psnr<<" ssim="<<ssim<<" failing_components="<<failures<<" first_failing_coordinate="<<(first_failure==SIZE_MAX?-1:int(first_failure%width))<<','<<(first_failure==SIZE_MAX?-1:int(first_failure/width))<<" worst_coordinate="<<worst%width<<','<<worst/width<<" expected=";print(expected[worst]);std::cerr<<" actual=";print(actual[worst]);std::cerr<<" cpu_primary_delta="<<digitor::primary_wheels_reference_count()-pw_before<<" cpu_curves_delta="<<digitor::cpu_curve_reference_count()-curve_before<<" fallback="<<(prov.primary_wheels_fallback_invocations+prov.curve_fallback_invocations)<<" intermediate_readback=0 intermediate_reupload=0 normal_readback="<<prov.normal_preview_readback_count<<" validation_readback="<<(read==DIGITOR_RESULT_OK)<<" dispatches=2 submissions=2 synchronizations=2\n";passed&=first_result==DIGITOR_RESULT_OK&&second_result==DIGITOR_RESULT_OK&&read==DIGITOR_RESULT_OK&&digitor::primary_wheels_reference_count()==pw_before&&digitor::cpu_curve_reference_count()==curve_before&&prov.primary_wheels_fallback_invocations==0&&prov.curve_fallback_invocations==0&&prov.normal_preview_readback_count==0&&failures==0&&rms<=5e-6&&ssim>=.99999;}
  return passed;
}

bool qualify_vulkan_failure_matrix(digitor::IRenderBackend& backend) {
  using F=digitor::GpuFailurePoint;
  constexpr std::uint32_t width=2,height=2;
  std::vector<digitor::Color> pixels(4,{.2f,.4f,.6f,1.f});
  auto wheels=digitor::PrimaryWheelsParameters::create();
  digitor::RgbCurvesParameters curve_desc;
  auto curves=digitor::CompiledRgbCurves::compile(curve_desc);
  const std::array process_common{
    F::ShaderCompilation,F::DescriptorSetLayoutCreation,F::PipelineLayoutCreation,
    F::PipelineCreation,F::OutputResourceCreation,F::OutputMemoryAllocation,
    F::OutputMemoryBinding,F::PreviewDestinationCreation,F::ImageViewCreation,
    F::ParameterResourceCreation,F::ParameterUpload,F::BufferMemoryAllocation,
    F::BufferMemoryBinding,F::DescriptorPoolCreation,F::DescriptorSetAllocation,
    F::DescriptorUpdate,F::ResourceBinding,F::CommandBufferOrListAllocation,
    F::CommandBufferOrListBeginReset,F::CommandRecording,F::DispatchOrDraw,
    F::CommandBufferOrListClose,F::QueueSubmission,F::SynchronizationWait,
    F::ProcessedFrameCreation,F::DeterministicOutOfMemory};
  const std::array cpu_source{F::SourceResourceCreation,F::SourceMemoryAllocation,
    F::SourceMemoryBinding,F::SourceUpload,F::SourceUploadRecording};
  const std::array curve_only{F::LutResourceCreation,F::LutUpload};
  const std::array preview_create{F::PreviewAcquisition,F::PreviewDestinationCreation,
    F::OutputMemoryAllocation,F::OutputMemoryBinding,F::ImageViewCreation,
    F::DeterministicOutOfMemory};
  const std::array preview_submit{F::PreviewPresentation,F::ConsumerCopySubmission,
    F::CommandBufferOrListAllocation,F::CommandBufferOrListBeginReset,
    F::CommandRecording,F::CommandBufferOrListClose,F::QueueSubmission,
    F::SynchronizationWait};
  const std::array validation{F::ValidationReadbackResourceCreation,
    F::BufferMemoryAllocation,F::BufferMemoryBinding,
    F::CommandBufferOrListAllocation,F::CommandBufferOrListBeginReset,
    F::CommandRecording,F::ValidationReadbackCopy,F::CommandBufferOrListClose,
    F::QueueSubmission,F::SynchronizationWait,F::ValidationReadbackMap,
    F::DeterministicOutOfMemory};
  auto contains=[](auto const& values,F point){return std::find(values.begin(),values.end(),point)!=values.end();};
  auto applicable=[&](std::string_view path,F point){
    if(path=="primary-cpu")return contains(process_common,point)||contains(cpu_source,point);
    if(path=="primary-gpu")return contains(process_common,point);
    if(path=="curves-cpu")return contains(process_common,point)||contains(cpu_source,point)||contains(curve_only,point);
    if(path=="curves-gpu")return contains(process_common,point)||contains(curve_only,point);
    if(path=="preview-create")return contains(preview_create,point);
    if(path=="preview-submit")return contains(preview_submit,point);
    return contains(validation,point);
  };
  const std::array paths{std::string_view{"primary-cpu"},std::string_view{"primary-gpu"},
    std::string_view{"curves-cpu"},std::string_view{"curves-gpu"},
    std::string_view{"preview-create"},std::string_view{"preview-submit"},
    std::string_view{"validation"}};
  bool passed=true;
  for(auto path:paths)for(auto point:digitor::all_gpu_failure_points()){
    if(point==F::DeviceLost){
      std::cerr<<"FAILURE_STAGE backend=Vulkan path="<<path<<" stage="<<digitor::gpu_failure_point_name(point)
        <<" classification=UNSUPPORTED reached=0 output_cleared=1 cleanup=1 cache_ok=1 cpu_primary_delta=0 cpu_curves_delta=0 fallback=0 intermediate_readback=0 intermediate_reupload=0 normal_readback=0 acquisition_balanced=1 recovery=0 reason=unsafe-device-loss-simulation\n";
      continue;
    }
    if(!applicable(path,point)){
      const char* reason="stage-not-used-by-this-vulkan-path";
      if(point==F::ShaderFunctionLookup)reason="SPIR-V-entry-point-resolution-occurs-in-vkCreateComputePipelines";
      else if(point==F::RootSignatureSerialization||point==F::RootSignatureCreation||
              point==F::DescriptorHeapCreation||point==F::ShaderResourceViewCreation||
              point==F::UnorderedAccessViewCreation)reason="D3D12-only-stage";
      else if(point==F::LibraryCreation||point==F::VertexShaderCreation||
              point==F::VertexShaderCompilation||point==F::FragmentShaderCreation||
              point==F::FragmentShaderCompilation||point==F::ProgramCreation||point==F::ProgramLink)
        reason="stage-does-not-exist-in-Vulkan-compute-path";
      else if(point==F::SourceResourceStorage||point==F::OutputResourceStorage||
              point==F::PreviewDestinationStorage||point==F::FramebufferCreation||
              point==F::FramebufferAttachment||point==F::FramebufferValidation||
              point==F::UniformLookup||point==F::DrawSetup||point==F::Flush)
        reason="GLES-only-stage";
      else if(point==F::LutUploadRecording||point==F::ParameterUploadRecording)
        reason="Vulkan-host-coherent-LUT-parameter-upload-has-no-command-copy";
      else if(point==F::ValidationTransition)reason="Vulkan-validation-readback-uses-a-general-memory-barrier-without-a-distinct-transition-stage";
      else if(point==F::FenceSignal)reason="Vulkan-path-uses-queue-idle-without-explicit-fence";
      std::cerr<<"FAILURE_STAGE backend=Vulkan path="<<path<<" stage="<<digitor::gpu_failure_point_name(point)
        <<" classification=NOT_APPLICABLE reached=0 output_cleared=1 cleanup=1 cache_ok=1 cpu_primary_delta=0 cpu_curves_delta=0 fallback=0 intermediate_readback=0 intermediate_reupload=0 normal_readback=0 acquisition_balanced=1 recovery=0 reason="<<reason<<"\n";
      continue;
    }
    digitor::ProcessedGpuFramePtr upstream;
    digitor::set_gpu_failure_point(F::None);
    if(backend.process_primary_wheels_gpu(pixels,width,height,700,*wheels,upstream)!=DIGITOR_RESULT_OK||!upstream)return false;
    std::shared_ptr<digitor::PreviewConsumerDestination> destination;
    if(path=="preview-submit"&&backend.create_preview_consumer(upstream,destination)!=DIGITOR_RESULT_OK)return false;
    if(point==F::ShaderCompilation||point==F::DescriptorSetLayoutCreation||
       point==F::PipelineLayoutCreation||point==F::PipelineCreation)
      backend.clear_native_pipeline_cache_for_test();
    digitor::ProcessedGpuFramePtr failed;
    digitor::ProcessedGpuFramePtr recovery_output;
    std::shared_ptr<digitor::PreviewConsumerDestination> failed_destination;
    std::shared_ptr<digitor::PreviewConsumerDestination> recovery_destination;
    std::vector<digitor::Color> readback(4);
    digitor::set_gpu_failure_point(point);
    DigitorResult result=DIGITOR_RESULT_INTERNAL_ERROR;
    auto invoke=[&](bool recovery){
      auto& output = recovery ? recovery_output : failed;
      auto& consumer = recovery ? recovery_destination : failed_destination;
      if(path=="primary-cpu")return backend.process_primary_wheels_gpu(pixels,width,height,recovery?702:701,*wheels,output);
      if(path=="primary-gpu")return backend.process_primary_wheels_gpu(backend.gpu_source(upstream),recovery?702:701,*wheels,output);
      if(path=="curves-cpu")return backend.process_curves_gpu(pixels,width,height,recovery?702:701,*curves,output);
      if(path=="curves-gpu")return backend.process_curves_gpu(backend.gpu_source(upstream),recovery?702:701,*curves,output);
      if(path=="preview-create")return backend.create_preview_consumer(upstream,consumer);
      if(path=="preview-submit")return destination->submit(upstream);
      return backend.validation_readback_primary_wheels(upstream,readback);
    };
    result=invoke(false);
    const auto evidence=backend.execution_provenance();
    failed.reset();failed_destination.reset();
    digitor::set_gpu_failure_point(F::None);
    const bool output_cleared=evidence.output_cleared&&(!failed)&&(!failed_destination);
    const auto recovery=invoke(true);
    const bool reached=evidence.requested_failure_point==point&&evidence.actual_stage_reached==point;
    const bool cpu_clean=evidence.cpu_primary_wheels_invocations==0&&evidence.cpu_curve_invocations==0;
    const bool fallback=evidence.primary_wheels_fallback_invocations||evidence.curve_fallback_invocations;
    const bool recovered=recovery==DIGITOR_RESULT_OK;
    const bool ok=result!=DIGITOR_RESULT_OK&&reached&&output_cleared&&evidence.cleanup_baseline&&
      evidence.cache_valid&&cpu_clean&&!fallback&&!evidence.normal_preview_readback_count&&recovered;
    std::cerr<<"FAILURE_STAGE backend=Vulkan path="<<path<<" stage="<<digitor::gpu_failure_point_name(point)
      <<" classification="<<(ok?"PASS":"FAIL")<<" reached="<<reached
      <<" output_cleared="<<output_cleared<<" cleanup="<<evidence.cleanup_baseline
      <<" cache_ok="<<evidence.cache_valid<<" cpu_primary_delta="<<evidence.cpu_primary_wheels_invocations
      <<" cpu_curves_delta="<<evidence.cpu_curve_invocations<<" fallback="<<fallback
      <<" intermediate_readback="<<evidence.intermediate_readback_count
      <<" intermediate_reupload="<<evidence.intermediate_reupload_count
      <<" normal_readback="<<evidence.normal_preview_readback_count
      <<" acquisition_balanced="<<(evidence.preview_acquisition_balance==0)
      <<" recovery="<<recovered<<'\n';
    passed&=ok;
    recovery_output.reset(); recovery_destination.reset();
    failed.reset(); failed_destination.reset(); destination.reset(); upstream.reset();
  }
  return passed;
}

bool qualify_vulkan_cache_failure(digitor::IRenderBackend& backend) {
  std::vector<digitor::Color> pixels(4,{.1f,.3f,.7f,1.f});
  auto parameters=digitor::PrimaryWheelsParameters::create();
  backend.clear_native_pipeline_cache_for_test();
  const auto before=backend.native_pipeline_cache_counters();
  const auto resources=backend.native_resource_counts();
  digitor::set_gpu_failure_point(digitor::GpuFailurePoint::PipelineCreation);
  digitor::ProcessedGpuFramePtr frame;
  const auto failure=backend.process_primary_wheels_gpu(pixels,2,2,801,*parameters,frame);
  const auto failed=backend.native_pipeline_cache_counters();
  const bool rejected=failure!=DIGITOR_RESULT_OK&&!frame&&failed.lookups==before.lookups+1&&
    failed.misses==before.misses+1&&failed.creation_failures==before.creation_failures+1&&
    failed.hits==before.hits&&backend.native_pipeline_cache_size()==0&&
    backend.native_resource_counts()==resources;
  digitor::set_gpu_failure_point(digitor::GpuFailurePoint::None);
  const auto retry=backend.process_primary_wheels_gpu(pixels,2,2,802,*parameters,frame);
  const auto created=backend.native_pipeline_cache_counters();
  digitor::ProcessedGpuFramePtr hit_frame;
  const auto hit=backend.process_primary_wheels_gpu(pixels,2,2,803,*parameters,hit_frame);
  const auto reused=backend.native_pipeline_cache_counters();
  const bool passed=rejected&&retry==DIGITOR_RESULT_OK&&frame&&
    created.misses==failed.misses+1&&created.creations==failed.creations+1&&
    backend.native_pipeline_cache_size()==1&&hit==DIGITOR_RESULT_OK&&hit_frame&&
    reused.hits==created.hits+1&&reused.creations==created.creations;
  std::cerr<<"VULKAN_CACHE_FAILURE lookup="<<(failed.lookups-before.lookups)
    <<" miss="<<(failed.misses-before.misses)
    <<" creation_failure="<<(failed.creation_failures-before.creation_failures)
    <<" inserted_after_failure="<<(rejected?0:1)
    <<" retry_creation="<<(created.creations-failed.creations)
    <<" subsequent_hit="<<(reused.hits-created.hits)
    <<" status="<<(passed?"PASS":"FAIL")<<'\n';
  return passed;
}

bool qualify_d3d12_failure_matrix(digitor::IRenderBackend& backend) {
  using F=digitor::GpuFailurePoint;std::vector<digitor::Color> pixels(4,{.2f,.4f,.6f,1.f});auto wheels=digitor::PrimaryWheelsParameters::create();digitor::RgbCurvesParameters cd;auto curves=digitor::CompiledRgbCurves::compile(cd);
  const std::array common{F::ShaderCompilation,F::RootSignatureSerialization,F::RootSignatureCreation,F::PipelineCreation,F::OutputResourceCreation,F::PreviewDestinationCreation,F::DescriptorHeapCreation,F::UnorderedAccessViewCreation,F::CommandAllocatorResetOrCreation,F::CommandBufferOrListBeginReset,F::CommandRecording,F::ResourceBinding,F::ParameterUpload,F::DispatchOrDraw,F::CommandBufferOrListClose,F::QueueSubmission,F::FenceSignal,F::EventSetup,F::SynchronizationWait,F::SynchronizationVerification,F::ProcessedFrameCreation,F::DeterministicOutOfMemory};
  const std::array cpu{F::SourceResourceCreation,F::SourceUpload,F::SourceUploadRecording,F::SourceTransition,F::CpuSourceShaderResourceViewCreation};const std::array curve{F::LutResourceCreation,F::LutUpload,F::LutShaderResourceViewCreation};const std::array gpu_source_view{F::GpuSourceShaderResourceViewCreation};
  const std::array pc{F::PreviewAcquisition,F::PreviewDestinationCreation,F::DeterministicOutOfMemory};const std::array ps{F::PreviewPresentation,F::ConsumerCopySubmission,F::SourceTransition,F::ConsumerDestinationTransition,F::CommandAllocatorResetOrCreation,F::CommandBufferOrListBeginReset,F::CommandBufferOrListClose,F::QueueSubmission,F::FenceSignal,F::EventSetup,F::SynchronizationWait,F::SynchronizationVerification};
  const std::array vr{F::ValidationReadbackResourceCreation,F::CommandAllocatorResetOrCreation,F::CommandBufferOrListBeginReset,F::ValidationTransition,F::ValidationReadbackCopy,F::CommandBufferOrListClose,F::QueueSubmission,F::FenceSignal,F::SynchronizationWait,F::ValidationReadbackMap,F::CpuReadbackCopy,F::EventSetup,F::SynchronizationVerification,F::DeterministicOutOfMemory};
  auto has=[](auto const&a,F p){return std::find(a.begin(),a.end(),p)!=a.end();};auto applies=[&](std::string_view path,F p){if(path=="primary-cpu")return has(common,p)||has(cpu,p);if(path=="primary-gpu")return has(common,p)||has(gpu_source_view,p);if(path=="curves-cpu")return has(common,p)||has(cpu,p)||has(curve,p);if(path=="curves-gpu")return has(common,p)||has(curve,p)||has(gpu_source_view,p);if(path=="preview-create")return has(pc,p);if(path=="preview-submit")return has(ps,p);return has(vr,p);};
  const std::array paths{std::string_view{"primary-cpu"},std::string_view{"primary-gpu"},std::string_view{"curves-cpu"},std::string_view{"curves-gpu"},std::string_view{"preview-create"},std::string_view{"preview-submit"},std::string_view{"validation"}};bool passed=true;
  for(auto path:paths)for(auto point:digitor::all_gpu_failure_points()){
    if(point==F::DeviceLost){std::cerr<<"FAILURE_STAGE backend=D3D12 path="<<path<<" stage="<<digitor::gpu_failure_point_name(point)<<" classification=UNSUPPORTED reached=0 output_cleared=1 cleanup=1 cache_ok=1 cpu_primary_delta=0 cpu_curves_delta=0 fallback=0 intermediate_readback=0 intermediate_reupload=0 normal_readback=0 acquisition_balanced=1 recovery=0 reason=unsafe-device-loss-simulation\n";continue;}
    if(!applies(path,point)){const char*reason="stage-not-used-by-this-D3D12-path";if(point==F::PipelineLayoutCreation||point==F::DescriptorSetLayoutCreation||point==F::DescriptorPoolCreation||point==F::DescriptorSetAllocation||point==F::ImageViewCreation)reason="Vulkan-only-resource-model";else if(point==F::LibraryCreation||point==F::ShaderFunctionLookup)reason="Metal-only-stage";else if(point==F::VertexShaderCreation||point==F::VertexShaderCompilation||point==F::FragmentShaderCreation||point==F::FragmentShaderCompilation||point==F::ProgramCreation||point==F::ProgramLink||point==F::FramebufferCreation||point==F::FramebufferAttachment||point==F::FramebufferValidation||point==F::UniformLookup||point==F::DrawSetup||point==F::Flush)reason="GLES-only-stage";else if(point==F::FenceCreation||point==F::EventCreation)reason="D3D12-backend-initialization-only-stage";else if(point==F::ParameterResourceCreation)reason="D3D12-parameters-use-root-constants-without-resource";else if(point==F::BufferMemoryAllocation||point==F::BufferMemoryBinding||point==F::SourceMemoryAllocation||point==F::SourceMemoryBinding||point==F::OutputMemoryAllocation||point==F::OutputMemoryBinding)reason="D3D12-committed-resources-combine-resource-and-memory";std::cerr<<"FAILURE_STAGE backend=D3D12 path="<<path<<" stage="<<digitor::gpu_failure_point_name(point)<<" classification=NOT_APPLICABLE reached=0 output_cleared=1 cleanup=1 cache_ok=1 cpu_primary_delta=0 cpu_curves_delta=0 fallback=0 intermediate_readback=0 intermediate_reupload=0 normal_readback=0 acquisition_balanced=1 recovery=0 reason="<<reason<<'\n';continue;}
    digitor::set_gpu_failure_point(F::None);digitor::ProcessedGpuFramePtr upstream;if(backend.process_primary_wheels_gpu(pixels,2,2,900,*wheels,upstream)!=DIGITOR_RESULT_OK||!upstream)return false;std::shared_ptr<digitor::PreviewConsumerDestination> destination;if(path=="preview-submit"&&backend.create_preview_consumer(upstream,destination)!=DIGITOR_RESULT_OK)return false;
    if(point==F::ShaderCompilation||point==F::RootSignatureSerialization||point==F::RootSignatureCreation||point==F::PipelineCreation)backend.clear_native_pipeline_cache_for_test();
    digitor::ProcessedGpuFramePtr output;std::shared_ptr<digitor::PreviewConsumerDestination> consumer;std::vector<digitor::Color> readback(4);auto invoke=[&](bool recovery){if(path=="primary-cpu")return backend.process_primary_wheels_gpu(pixels,2,2,recovery?902:901,*wheels,output);if(path=="primary-gpu")return backend.process_primary_wheels_gpu(backend.gpu_source(upstream),recovery?902:901,*wheels,output);if(path=="curves-cpu")return backend.process_curves_gpu(pixels,2,2,recovery?902:901,*curves,output);if(path=="curves-gpu")return backend.process_curves_gpu(backend.gpu_source(upstream),recovery?902:901,*curves,output);if(path=="preview-create")return backend.create_preview_consumer(upstream,consumer);if(path=="preview-submit")return destination->submit(upstream);return backend.validation_readback_primary_wheels(upstream,readback);};
    digitor::set_gpu_failure_point(point);const auto failure=invoke(false);const auto evidence=backend.execution_provenance();output.reset();consumer.reset();digitor::set_gpu_failure_point(F::None);const auto recovery=invoke(true);const bool reached=evidence.requested_failure_point==point&&evidence.actual_stage_reached==point;const bool clean=evidence.cpu_primary_wheels_invocations==0&&evidence.cpu_curve_invocations==0;const bool fallback=evidence.primary_wheels_fallback_invocations||evidence.curve_fallback_invocations;const bool ok=failure!=DIGITOR_RESULT_OK&&reached&&evidence.output_cleared&&evidence.cleanup_baseline&&evidence.cache_valid&&clean&&!fallback&&!evidence.normal_preview_readback_count&&recovery==DIGITOR_RESULT_OK;
    std::cerr<<"FAILURE_STAGE backend=D3D12 path="<<path<<" stage="<<digitor::gpu_failure_point_name(point)<<" classification="<<(ok?"PASS":"FAIL")<<" reached="<<reached<<" output_cleared="<<evidence.output_cleared<<" cleanup="<<evidence.cleanup_baseline<<" cache_ok="<<evidence.cache_valid<<" cpu_primary_delta="<<evidence.cpu_primary_wheels_invocations<<" cpu_curves_delta="<<evidence.cpu_curve_invocations<<" fallback="<<fallback<<" intermediate_readback="<<evidence.intermediate_readback_count<<" intermediate_reupload="<<evidence.intermediate_reupload_count<<" normal_readback="<<evidence.normal_preview_readback_count<<" acquisition_balanced="<<(evidence.preview_acquisition_balance==0)<<" recovery="<<(recovery==DIGITOR_RESULT_OK)<<'\n';passed&=ok;
  }return passed;
}

bool qualify_d3d12_cache_failures(digitor::IRenderBackend&backend){using F=digitor::GpuFailurePoint;std::vector<digitor::Color>pixels(4,{.1f,.3f,.7f,1.f});auto wheels=digitor::PrimaryWheelsParameters::create();digitor::RgbCurvesParameters cd;auto curves=digitor::CompiledRgbCurves::compile(cd);bool passed=true;for(bool curve:{false,true})for(auto point:{F::ShaderCompilation,F::RootSignatureSerialization,F::RootSignatureCreation,F::PipelineCreation}){backend.clear_native_pipeline_cache_for_test();auto before=backend.native_pipeline_cache_counters();auto resources=backend.native_resource_counts();digitor::set_gpu_failure_point(point);digitor::ProcessedGpuFramePtr frame;auto failure=curve?backend.process_curves_gpu(pixels,2,2,950,*curves,frame):backend.process_primary_wheels_gpu(pixels,2,2,950,*wheels,frame);auto failed=backend.native_pipeline_cache_counters();bool rejected=failure!=DIGITOR_RESULT_OK&&!frame&&failed.lookups==before.lookups+1&&failed.misses==before.misses+1&&failed.creation_failures==before.creation_failures+1&&failed.hits==before.hits&&backend.native_pipeline_cache_size()==0&&backend.native_resource_counts()==resources;digitor::set_gpu_failure_point(F::None);auto retry=curve?backend.process_curves_gpu(pixels,2,2,951,*curves,frame):backend.process_primary_wheels_gpu(pixels,2,2,951,*wheels,frame);auto created=backend.native_pipeline_cache_counters();digitor::ProcessedGpuFramePtr hitframe;auto hit=curve?backend.process_curves_gpu(pixels,2,2,952,*curves,hitframe):backend.process_primary_wheels_gpu(pixels,2,2,952,*wheels,hitframe);auto reused=backend.native_pipeline_cache_counters();bool ok=rejected&&retry==DIGITOR_RESULT_OK&&frame&&created.misses==failed.misses+1&&created.creations==failed.creations+1&&hit==DIGITOR_RESULT_OK&&hitframe&&reused.hits==created.hits+1&&reused.creations==created.creations;std::cerr<<"D3D12_CACHE_FAILURE operation="<<(curve?"rgb-curves":"primary-wheels")<<" stage="<<digitor::gpu_failure_point_name(point)<<" lookup="<<(failed.lookups-before.lookups)<<" miss="<<(failed.misses-before.misses)<<" creation_failure="<<(failed.creation_failures-before.creation_failures)<<" retry_creation="<<(created.creations-failed.creations)<<" subsequent_hit="<<(reused.hits-created.hits)<<" status="<<(ok?"PASS":"FAIL")<<'\n';passed&=ok;}return passed;}

bool qualify_metal_failure_matrix(digitor::IRenderBackend&backend){using F=digitor::GpuFailurePoint;std::vector<digitor::Color>pixels(4,{.2f,.4f,.6f,1.f});auto wheels=digitor::PrimaryWheelsParameters::create();digitor::RgbCurvesParameters cd;auto curves=digitor::CompiledRgbCurves::compile(cd);const std::array common{F::LibraryCreation,F::ShaderFunctionLookup,F::PipelineCreation,F::OutputResourceCreation,F::PreviewDestinationCreation,F::CommandQueueCreation,F::CommandBufferOrListAllocation,F::ComputeEncoderCreation,F::SourceTextureBinding,F::OutputTextureBinding,F::BufferBinding,F::DispatchSetup,F::DispatchOrDraw,F::EncoderCompletion,F::QueueSubmission,F::SynchronizationWait,F::CommandStatusVerification,F::ProcessedFrameCreation,F::DeterministicOutOfMemory};const std::array cpu{F::SourceResourceCreation,F::SourceUpload};const std::array primary{F::ParameterResourceCreation};const std::array curve{F::LutResourceCreation,F::LutUpload};const std::array pc{F::PreviewAcquisition,F::PreviewDestinationCreation,F::CommandQueueCreation,F::DeterministicOutOfMemory};const std::array ps{F::CommandBufferOrListAllocation,F::BlitEncoderCreation,F::ResourceBinding,F::PreviewPresentation,F::ConsumerCopySubmission,F::EncoderCompletion,F::QueueSubmission,F::SynchronizationWait,F::CommandStatusVerification};const std::array vr{F::ValidationReadbackResourceCreation,F::CommandBufferOrListAllocation,F::BlitEncoderCreation,F::ValidationReadbackCopy,F::EncoderCompletion,F::QueueSubmission,F::SynchronizationWait,F::CommandStatusVerification,F::ValidationCpuCopy,F::DeterministicOutOfMemory};auto has=[](auto const&a,F p){return std::find(a.begin(),a.end(),p)!=a.end();};auto applies=[&](std::string_view path,F p){if(path=="primary-cpu")return has(common,p)||has(cpu,p)||has(primary,p);if(path=="primary-gpu")return has(common,p)||has(primary,p);if(path=="curves-cpu")return has(common,p)||has(cpu,p)||has(curve,p);if(path=="curves-gpu")return has(common,p)||has(curve,p);if(path=="preview-create")return has(pc,p);if(path=="preview-submit")return has(ps,p);return has(vr,p);};const std::array paths{std::string_view{"primary-cpu"},std::string_view{"primary-gpu"},std::string_view{"curves-cpu"},std::string_view{"curves-gpu"},std::string_view{"preview-create"},std::string_view{"preview-submit"},std::string_view{"validation"}};bool passed=true;for(auto path:paths)for(auto point:digitor::all_gpu_failure_points()){if(point==F::DeviceLost){std::cerr<<"FAILURE_STAGE backend=Metal path="<<path<<" stage="<<digitor::gpu_failure_point_name(point)<<" classification=UNSUPPORTED reached=0 output_cleared=1 cleanup=1 cache_ok=1 cpu_primary_delta=0 cpu_curves_delta=0 fallback=0 intermediate_readback=0 intermediate_reupload=0 normal_readback=0 acquisition_balanced=1 recovery=0 reason=unsafe-device-loss-simulation\n";continue;}if(!applies(path,point)){const char*reason="stage-not-used-by-this-Metal-path";if(point==F::DescriptorSetLayoutCreation||point==F::PipelineLayoutCreation||point==F::DescriptorPoolCreation||point==F::DescriptorSetAllocation||point==F::ImageViewCreation)reason="Vulkan-only-stage";else if(point==F::RootSignatureSerialization||point==F::RootSignatureCreation||point==F::DescriptorHeapCreation||point==F::ShaderResourceViewCreation||point==F::UnorderedAccessViewCreation||point==F::FenceSignal||point==F::FenceCreation||point==F::EventCreation||point==F::EventSetup||point==F::CpuSourceShaderResourceViewCreation||point==F::GpuSourceShaderResourceViewCreation||point==F::LutShaderResourceViewCreation||point==F::SourceTransition||point==F::ConsumerDestinationTransition||point==F::ValidationTransition||point==F::CpuReadbackCopy)reason="D3D12-only-stage";else if(point==F::FramebufferCreation||point==F::FramebufferAttachment||point==F::FramebufferValidation||point==F::UniformLookup||point==F::DrawSetup||point==F::Flush)reason="GLES-only-stage";else if(point==F::CommandPoolCreation)reason="Metal-uses-command-queues-without-command-pools";else if(point==F::ParameterUpload)reason="Metal-primary-parameters-are-uploaded-during-buffer-creation";else if(point==F::SourceUpload&&path.find("gpu")!=std::string_view::npos)reason="Metal-GPU-intermediate-source-requires-no-upload";std::cerr<<"FAILURE_STAGE backend=Metal path="<<path<<" stage="<<digitor::gpu_failure_point_name(point)<<" classification=NOT_APPLICABLE reached=0 output_cleared=1 cleanup=1 cache_ok=1 cpu_primary_delta=0 cpu_curves_delta=0 fallback=0 intermediate_readback=0 intermediate_reupload=0 normal_readback=0 acquisition_balanced=1 recovery=0 reason="<<reason<<'\n';continue;}digitor::set_gpu_failure_point(F::None);digitor::ProcessedGpuFramePtr upstream;if(backend.process_primary_wheels_gpu(pixels,2,2,1000,*wheels,upstream)!=DIGITOR_RESULT_OK||!upstream)return false;std::shared_ptr<digitor::PreviewConsumerDestination>destination;if(path=="preview-submit"&&backend.create_preview_consumer(upstream,destination)!=DIGITOR_RESULT_OK)return false;if(point==F::LibraryCreation||point==F::ShaderFunctionLookup||point==F::PipelineCreation)backend.clear_native_pipeline_cache_for_test();digitor::ProcessedGpuFramePtr output;std::shared_ptr<digitor::PreviewConsumerDestination>consumer;std::vector<digitor::Color>readback(4);auto invoke=[&](bool recovery){if(path=="primary-cpu")return backend.process_primary_wheels_gpu(pixels,2,2,recovery?1002:1001,*wheels,output);if(path=="primary-gpu")return backend.process_primary_wheels_gpu(backend.gpu_source(upstream),recovery?1002:1001,*wheels,output);if(path=="curves-cpu")return backend.process_curves_gpu(pixels,2,2,recovery?1002:1001,*curves,output);if(path=="curves-gpu")return backend.process_curves_gpu(backend.gpu_source(upstream),recovery?1002:1001,*curves,output);if(path=="preview-create")return backend.create_preview_consumer(upstream,consumer);if(path=="preview-submit")return destination->submit(upstream);return backend.validation_readback_primary_wheels(upstream,readback);};digitor::set_gpu_failure_point(point);auto failure=invoke(false);auto evidence=backend.execution_provenance();output.reset();consumer.reset();digitor::set_gpu_failure_point(F::None);auto recovery=invoke(true);bool reached=evidence.requested_failure_point==point&&evidence.actual_stage_reached==point;bool clean=evidence.cpu_primary_wheels_invocations==0&&evidence.cpu_curve_invocations==0;bool fallback=evidence.primary_wheels_fallback_invocations||evidence.curve_fallback_invocations;bool ok=failure!=DIGITOR_RESULT_OK&&reached&&evidence.output_cleared&&evidence.cleanup_baseline&&evidence.cache_valid&&clean&&!fallback&&!evidence.normal_preview_readback_count&&recovery==DIGITOR_RESULT_OK;std::cerr<<"FAILURE_STAGE backend=Metal path="<<path<<" stage="<<digitor::gpu_failure_point_name(point)<<" classification="<<(ok?"PASS":"FAIL")<<" reached="<<reached<<" output_cleared="<<evidence.output_cleared<<" cleanup="<<evidence.cleanup_baseline<<" cache_ok="<<evidence.cache_valid<<" cpu_primary_delta="<<evidence.cpu_primary_wheels_invocations<<" cpu_curves_delta="<<evidence.cpu_curve_invocations<<" fallback="<<fallback<<" intermediate_readback="<<evidence.intermediate_readback_count<<" intermediate_reupload="<<evidence.intermediate_reupload_count<<" normal_readback="<<evidence.normal_preview_readback_count<<" acquisition_balanced="<<(evidence.preview_acquisition_balance==0)<<" recovery="<<(recovery==DIGITOR_RESULT_OK)<<'\n';passed&=ok;}return passed;}

bool qualify_metal_cache_failures(digitor::IRenderBackend&backend){using F=digitor::GpuFailurePoint;std::vector<digitor::Color>pixels(4,{.1f,.3f,.7f,1.f});auto wheels=digitor::PrimaryWheelsParameters::create();digitor::RgbCurvesParameters cd;auto curves=digitor::CompiledRgbCurves::compile(cd);bool passed=true;for(bool curve:{false,true})for(auto point:{F::LibraryCreation,F::ShaderFunctionLookup,F::PipelineCreation}){backend.clear_native_pipeline_cache_for_test();auto before=backend.native_pipeline_cache_counters();auto resources=backend.native_resource_counts();digitor::set_gpu_failure_point(point);digitor::ProcessedGpuFramePtr frame;auto failure=curve?backend.process_curves_gpu(pixels,2,2,1050,*curves,frame):backend.process_primary_wheels_gpu(pixels,2,2,1050,*wheels,frame);auto failed=backend.native_pipeline_cache_counters();bool rejected=failure!=DIGITOR_RESULT_OK&&!frame&&failed.lookups==before.lookups+1&&failed.misses==before.misses+1&&failed.creation_failures==before.creation_failures+1&&failed.hits==before.hits&&backend.native_pipeline_cache_size()==0&&backend.native_resource_counts()==resources;digitor::set_gpu_failure_point(F::None);auto retry=curve?backend.process_curves_gpu(pixels,2,2,1051,*curves,frame):backend.process_primary_wheels_gpu(pixels,2,2,1051,*wheels,frame);auto created=backend.native_pipeline_cache_counters();digitor::ProcessedGpuFramePtr hitframe;auto hit=curve?backend.process_curves_gpu(pixels,2,2,1052,*curves,hitframe):backend.process_primary_wheels_gpu(pixels,2,2,1052,*wheels,hitframe);auto reused=backend.native_pipeline_cache_counters();bool ok=rejected&&retry==DIGITOR_RESULT_OK&&frame&&created.misses==failed.misses+1&&created.creations==failed.creations+1&&hit==DIGITOR_RESULT_OK&&hitframe&&reused.hits==created.hits+1&&reused.creations==created.creations;std::cerr<<"METAL_CACHE_FAILURE operation="<<(curve?"rgb-curves":"primary-wheels")<<" stage="<<digitor::gpu_failure_point_name(point)<<" lookup="<<(failed.lookups-before.lookups)<<" miss="<<(failed.misses-before.misses)<<" creation_failure="<<(failed.creation_failures-before.creation_failures)<<" retry_creation="<<(created.creations-failed.creations)<<" subsequent_hit="<<(reused.hits-created.hits)<<" status="<<(ok?"PASS":"FAIL")<<'\n';passed&=ok;}return passed;}

bool exercise(digitor::IRenderBackend& backend, std::string_view name) {
    constexpr std::array dimensions{std::pair{1u,1u}, std::pair{2u,2u}, std::pair{3u,2u},
        std::pair{7u,5u}, std::pair{63u,17u}, std::pair{65u,3u}, std::pair{257u,2u}};
    constexpr std::array colors{Pixel{255,0,0,255}, Pixel{0,255,0,255}, Pixel{0,0,255,255},
        Pixel{0,0,0,255}, Pixel{17,53,101,211}};
    bool passed = true;
    passed &= qualify_primary_wheels(backend,name);
    passed &= qualify_composition(backend,name);
    std::vector<digitor::Color> grade_input{{.0f,.25f,1.f,1.f},{.1f,.5f,.9f,.75f},
        {-.1f,1.2f,.33f,.5f}}, grade_cpu(grade_input.size()), grade_gpu(grade_input.size());
    digitor::ColorGrade grade{.exposure=.25f,.contrast=1.1f,.gamma=.95f,.lift=.02f,
        .gain=1.03f,.offset=-.01f,.temperature=.15f,.tint=-.1f,.saturation=.8f};
    digitor::grade_image_cpu(grade_input.data(),grade_cpu.data(),grade_input.size(),grade);
    const auto grade_result=backend.grade_rgba32f(grade_input,grade_gpu,grade);
    const auto& provenance = backend.execution_provenance();
    double maximum=0,squared=0;
    for(std::size_t n=0;n<grade_cpu.size();++n)for(int c=0;c<4;++c){const float*a=&grade_cpu[n].r,*b=&grade_gpu[n].r;double error=std::abs(double(a[c])-b[c]);maximum=std::max(maximum,error);squared+=error*error;}
    const double rms=std::sqrt(squared/(grade_cpu.size()*4));
    const double psnr=rms==0?INFINITY:20*std::log10(1/rms);
    std::uint32_t grade_width=0;
    if(!digitor::checked_size_to_uint32(grade_cpu.size(),grade_width))return false;
    digitor::VideoFrame reference{.width=grade_width,.height=1,.pixels=grade_cpu};
    digitor::VideoFrame actual{.width=grade_width,.height=1,.pixels=grade_gpu};
    const double ssim=digitor::calculate_ssim(reference,actual);
    std::cerr<<"COLOR METRICS backend="<<name<<" max_absolute_error="<<maximum
             <<" rms_error="<<rms<<" psnr="<<psnr<<" ssim="<<ssim<<'\n';
    passed &= grade_result==DIGITOR_RESULT_OK && maximum<2e-5 && ssim>.99999;
    passed &= provenance.gpu_execution && provenance.source_upload_performed &&
        provenance.command_recorded && provenance.dispatch_or_draw_issued &&
        provenance.queue_submission_issued && provenance.synchronization_waited &&
        provenance.output_written && provenance.readback_performed &&
        provenance.cpu_fallback_invocations == 0 &&
        provenance.cpu_color_reference_invocations == 0;
    digitor::RgbCurvesParameters curve_parameters;
    curve_parameters.master.points={{0,0},{.3f,.18f},{.72f,.84f},{1,1}};
    curve_parameters.red.points={{0,0},{.5f,.62f},{1,1}};
    const auto curves=digitor::CompiledRgbCurves::compile(curve_parameters);
    std::vector<digitor::Color> curve_cpu(grade_input.size()),curve_gpu(grade_input.size());
    curves->apply(grade_input,curve_cpu);
    const auto curve_result=backend.curves_rgba32f(grade_input,curve_gpu,*curves);
    double curve_max=0,curve_relative=0,curve_squared=0;std::size_t worst=0;
    for(std::size_t n=0;n<curve_cpu.size();++n)for(int c=0;c<4;++c){const float*a=&curve_cpu[n].r,*b=&curve_gpu[n].r;const double error=std::abs(double(a[c])-b[c]);if(error>curve_max){curve_max=error;worst=n;}curve_relative=std::max(curve_relative,error/std::max(1e-7,std::abs(double(a[c]))));curve_squared+=error*error;}
    const double curve_rms=std::sqrt(curve_squared/(curve_cpu.size()*4));
    std::uint32_t curve_width=0;
    if(!digitor::checked_size_to_uint32(curve_cpu.size(),curve_width))return false;
    digitor::VideoFrame curve_reference{.width=curve_width,.height=1,.pixels=curve_cpu};
    digitor::VideoFrame curve_actual{.width=curve_width,.height=1,.pixels=curve_gpu};
    const double curve_psnr=curve_rms==0?INFINITY:20*std::log10(1/curve_rms),curve_ssim=digitor::calculate_ssim(curve_reference,curve_actual);
    std::cerr<<"RGB CURVES EXECUTION backend="<<name<<" result="<<curve_result<<" failure_stage=\""<<backend.execution_provenance().failure_stage<<"\"\n";
    std::cerr<<"RGB CURVES METRICS backend="<<name<<" max_error="<<curve_max<<" relative_error="<<curve_relative<<" rms="<<curve_rms<<" psnr="<<curve_psnr<<" ssim="<<curve_ssim<<" worst_pixel="<<worst<<'\n';
    const auto& curve_provenance=backend.execution_provenance();
    passed &= curve_result==DIGITOR_RESULT_OK && curve_max<2e-5 && curve_ssim>.99999 &&
      curve_provenance.dispatch_or_draw_issued && curve_provenance.validation_readback_completed &&
      curve_provenance.cpu_curve_invocations==0 && curve_provenance.curve_fallback_invocations==0;
    digitor::ProcessedGpuFramePtr processed;
    const auto processed_result=backend.process_curves_gpu(grade_input,3,1,73,*curves,processed);
    const auto producer_identity=processed?processed->identity():0;
    const auto present_result=processed?backend.present_gpu_frame(processed):DIGITOR_RESULT_INTERNAL_ERROR;
    const auto& preview_provenance=backend.execution_provenance();
    std::cerr<<"DIRECT PREVIEW backend="<<name<<" producer_identity="<<producer_identity
             <<" consumer_identity="<<(processed?processed->identity():0)
             <<" normal_readback="<<preview_provenance.readback_performed
             <<" cpu_curve_delta="<<preview_provenance.cpu_curve_invocations
             <<" fallback="<<preview_provenance.curve_fallback_invocations<<'\n';
    passed &= processed_result==DIGITOR_RESULT_OK && processed && processed->ready() &&
      present_result==DIGITOR_RESULT_OK && producer_identity==processed->identity() &&
      preview_provenance.preview_source==digitor::PreviewSource::gpu &&
      preview_provenance.direct_preview_consumed && !preview_provenance.readback_performed &&
      preview_provenance.cpu_curve_invocations==0 && preview_provenance.curve_fallback_invocations==0;
    digitor::set_gpu_failure_point(digitor::GpuFailurePoint::None);
    for (auto [width, height] : dimensions) {
        for (const auto color : colors) {
            const auto expected = solid(width, height, color);
            std::vector<std::uint8_t> actual;
            const auto result = backend.render_rgba8(width, height, expected, actual);
            if (result != DIGITOR_RESULT_OK) {
                std::cerr << "RENDER FAILURE backend=" << name << " operation=upload/copy dimensions="
                          << width << 'x' << height << " result=" << result << '\n';
                passed = false;
            } else passed &= compare(name, "upload/copy", width, height, expected, actual);
        }
        const auto expected_pattern = pattern(width, height);
        std::vector<std::uint8_t> actual;
        const auto upload_result = backend.render_rgba8(width, height, expected_pattern, actual);
        if (upload_result != DIGITOR_RESULT_OK) passed = false;
        else passed &= compare(name, "gradient upload/copy", width, height, expected_pattern, actual);

        const auto expected_clear = solid(width, height, Pixel{0,0,0,255});
        actual.clear();
        const auto clear_result = backend.render_rgba8(width, height, {}, actual);
        if (clear_result != DIGITOR_RESULT_OK) {
            std::cerr << "RENDER FAILURE backend=" << name << " operation=clear dimensions="
                      << width << 'x' << height << " result=" << clear_result << '\n';
            passed = false;
        } else passed &= compare(name, "clear", width, height, expected_clear, actual);
    }
    std::cerr << "BACKEND RESULT backend=" << name << " status=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed;
}
} // namespace


bool qualify_gles_cache_failures(digitor::IRenderBackend& backend) {
  using F=digitor::GpuFailurePoint;
  std::vector<digitor::Color> pixels(4,{.1f,.3f,.7f,1.f});
  auto wheels=digitor::PrimaryWheelsParameters::create();
  digitor::RgbCurvesParameters cd; auto curves=digitor::CompiledRgbCurves::compile(cd);
  bool passed=true;
  const std::array points{F::VertexShaderCreation,F::VertexShaderCompilation,F::FragmentShaderCreation,F::FragmentShaderCompilation,F::ProgramCreation,F::ProgramLink};
  for(bool curve:{false,true}) for(auto point:points){
    backend.clear_native_pipeline_cache_for_test();
    const auto before=backend.native_pipeline_cache_counters();
    const auto resources=backend.native_resource_counts();
    digitor::set_gpu_failure_point(point); digitor::ProcessedGpuFramePtr frame;
    const auto failure=curve?backend.process_curves_gpu(pixels,2,2,1200,*curves,frame):backend.process_primary_wheels_gpu(pixels,2,2,1200,*wheels,frame);
    const auto failed=backend.native_pipeline_cache_counters();
    const bool rejected=failure!=DIGITOR_RESULT_OK&&!frame&&failed.lookups==before.lookups+1&&failed.misses==before.misses+1&&failed.creation_failures==before.creation_failures+1&&failed.hits==before.hits&&backend.native_pipeline_cache_size()==0&&backend.native_resource_counts()==resources;
    digitor::set_gpu_failure_point(F::None);
    const auto retry=curve?backend.process_curves_gpu(pixels,2,2,1201,*curves,frame):backend.process_primary_wheels_gpu(pixels,2,2,1201,*wheels,frame);
    const auto created=backend.native_pipeline_cache_counters(); digitor::ProcessedGpuFramePtr hitframe;
    const auto hit=curve?backend.process_curves_gpu(pixels,2,2,1202,*curves,hitframe):backend.process_primary_wheels_gpu(pixels,2,2,1202,*wheels,hitframe);
    const auto reused=backend.native_pipeline_cache_counters();
    const bool ok=rejected&&retry==DIGITOR_RESULT_OK&&frame&&created.creations==failed.creations+1&&hit==DIGITOR_RESULT_OK&&hitframe&&reused.hits==created.hits+1&&reused.creations==created.creations;
    std::cerr<<"GLES_CACHE_FAILURE operation="<<(curve?"rgb-curves":"primary-wheels")<<" stage="<<digitor::gpu_failure_point_name(point)<<" status="<<(ok?"PASS":"FAIL")<<'\n'; passed&=ok;
  }
  return passed;
}

bool qualify_gles_failure_matrix(digitor::IRenderBackend& backend) {
  using F=digitor::GpuFailurePoint;
  std::vector<digitor::Color> pixels(4,{.2f,.4f,.6f,1.f}); auto wheels=digitor::PrimaryWheelsParameters::create();
  digitor::RgbCurvesParameters cd; auto curves=digitor::CompiledRgbCurves::compile(cd);
  const std::array pipeline{F::VertexShaderCreation,F::VertexShaderCompilation,F::FragmentShaderCreation,F::FragmentShaderCompilation,F::ProgramCreation,F::ProgramLink};
  const std::array common{F::OutputResourceCreation,F::OutputResourceStorage,F::FramebufferCreation,F::FramebufferAttachment,F::FramebufferValidation,F::ResourceBinding,F::UniformLookup,F::DrawSetup,F::DispatchOrDraw,F::ProcessedFrameCreation,F::DeterministicOutOfMemory};
  const std::array cpu{F::SourceResourceCreation,F::SourceResourceStorage,F::SourceUpload,F::Flush};
  const std::array curve{F::LutResourceCreation,F::LutUpload};
  const std::array consumer_create{F::PreviewAcquisition,F::PreviewDestinationCreation,F::PreviewDestinationStorage,F::FramebufferCreation,F::FramebufferAttachment,F::FramebufferValidation,F::DeterministicOutOfMemory};
  const std::array consumer_submit{F::ResourceBinding,F::PreviewPresentation,F::ConsumerCopySubmission,F::SynchronizationWait};
  const std::array validation{F::ValidationReadbackResourceCreation,F::ValidationReadbackCopy,F::ValidationReadbackMap};
  auto has=[](auto const&a,F p){return std::find(a.begin(),a.end(),p)!=a.end();};
  auto applies=[&](std::string_view path,F point){if(path=="primary-cpu")return has(pipeline,point)||has(common,point)||has(cpu,point);if(path=="primary-gpu")return has(pipeline,point)||has(common,point)||point==F::SynchronizationWait;if(path=="curves-cpu")return has(pipeline,point)||has(common,point)||has(cpu,point)||has(curve,point);if(path=="curves-gpu")return has(pipeline,point)||has(common,point)||has(curve,point)||point==F::SynchronizationWait;if(path=="preview-create")return has(consumer_create,point);if(path=="preview-submit")return has(consumer_submit,point);return has(validation,point);};
  const std::array paths{std::string_view{"primary-cpu"},std::string_view{"primary-gpu"},std::string_view{"curves-cpu"},std::string_view{"curves-gpu"},std::string_view{"preview-create"},std::string_view{"preview-submit"},std::string_view{"validation"}};
  bool passed=true;
  for(auto path:paths) for(auto point:digitor::all_gpu_failure_points()){
    if(point==F::DeviceLost){std::cerr<<"FAILURE_STAGE backend=GLES path="<<path<<" stage=DeviceLost classification=UNSUPPORTED reached=0 output_cleared=1 cleanup=1 cache_ok=1 cpu_primary_delta=0 cpu_curves_delta=0 fallback=0 intermediate_readback=0 intermediate_reupload=0 normal_readback=0 acquisition_balanced=1 recovery=0 reason=unsafe-device-loss-simulation\n";continue;}
    if(!applies(path,point)){std::cerr<<"FAILURE_STAGE backend=GLES path="<<path<<" stage="<<digitor::gpu_failure_point_name(point)<<" classification=NOT_APPLICABLE reached=0 output_cleared=1 cleanup=1 cache_ok=1 cpu_primary_delta=0 cpu_curves_delta=0 fallback=0 intermediate_readback=0 intermediate_reupload=0 normal_readback=0 acquisition_balanced=1 recovery=0 reason=stage-not-used-by-this-GLES-path\n";continue;}
    digitor::set_gpu_failure_point(F::None); digitor::ProcessedGpuFramePtr upstream;
    if(backend.process_primary_wheels_gpu(pixels,2,2,1210,*wheels,upstream)!=DIGITOR_RESULT_OK||!upstream)return false;
    std::shared_ptr<digitor::PreviewConsumerDestination> destination; if(path=="preview-submit"&&backend.create_preview_consumer(upstream,destination)!=DIGITOR_RESULT_OK)return false;
    if(has(pipeline,point)) backend.clear_native_pipeline_cache_for_test();
    const auto baseline=backend.native_resource_counts();
    digitor::ProcessedGpuFramePtr failed_output, recovery_output;
    std::shared_ptr<digitor::PreviewConsumerDestination> failed_consumer, recovery_consumer;
    std::vector<digitor::Color> readback(4);
    auto invoke=[&](bool recovery){
      auto& output = recovery ? recovery_output : failed_output;
      auto& consumer = recovery ? recovery_consumer : failed_consumer;
      if(path=="primary-cpu")return backend.process_primary_wheels_gpu(pixels,2,2,recovery?1212:1211,*wheels,output);
      if(path=="primary-gpu")return backend.process_primary_wheels_gpu(backend.gpu_source(upstream),recovery?1212:1211,*wheels,output);
      if(path=="curves-cpu")return backend.process_curves_gpu(pixels,2,2,recovery?1212:1211,*curves,output);
      if(path=="curves-gpu")return backend.process_curves_gpu(backend.gpu_source(upstream),recovery?1212:1211,*curves,output);
      if(path=="preview-create")return backend.create_preview_consumer(upstream,consumer);
      if(path=="preview-submit")return destination->submit(upstream);
      return backend.validation_readback_primary_wheels(upstream,readback);
    };
    digitor::set_gpu_failure_point(point);
    const auto failure=invoke(false);
    const auto evidence=backend.execution_provenance();
    failed_output.reset(); failed_consumer.reset();
    const bool cleanup=backend.native_resource_counts()==baseline;
    digitor::set_gpu_failure_point(F::None);
    const auto recovery=invoke(true);
    const bool reached=evidence.requested_failure_point==point&&evidence.actual_stage_reached==point; const bool fallback=evidence.primary_wheels_fallback_invocations||evidence.curve_fallback_invocations; const bool ok=failure!=DIGITOR_RESULT_OK&&reached&&evidence.output_cleared&&cleanup&&evidence.cache_valid&&!fallback&&!evidence.normal_preview_readback_count&&recovery==DIGITOR_RESULT_OK;
    std::cerr<<"FAILURE_STAGE backend=GLES path="<<path<<" stage="<<digitor::gpu_failure_point_name(point)<<" classification="<<(ok?"PASS":"FAIL")<<" reached="<<reached<<" output_cleared="<<evidence.output_cleared<<" cleanup="<<cleanup<<" cache_ok="<<evidence.cache_valid<<" cpu_primary_delta="<<evidence.cpu_primary_wheels_invocations<<" cpu_curves_delta="<<evidence.cpu_curve_invocations<<" fallback="<<fallback<<" intermediate_readback="<<evidence.intermediate_readback_count<<" intermediate_reupload="<<evidence.intermediate_reupload_count<<" normal_readback="<<evidence.normal_preview_readback_count<<" acquisition_balanced="<<(evidence.preview_acquisition_balance==0)<<" recovery="<<(recovery==DIGITOR_RESULT_OK)<<'\n'; recovery_output.reset(); recovery_consumer.reset(); destination.reset(); upstream.reset(); passed&=ok;
  }
  return passed;
}

int main() {
#if defined(_WIN32)
    constexpr std::array entries{std::pair{DIGITOR_RENDERER_D3D12, std::string_view{"Direct3D12"}},
                                 std::pair{DIGITOR_RENDERER_VULKAN, std::string_view{"Vulkan"}}};
#elif defined(__APPLE__)
    constexpr std::array entries{std::pair{DIGITOR_RENDERER_METAL, std::string_view{"Metal"}}};
#elif defined(__ANDROID__)
    constexpr std::array entries{std::pair{DIGITOR_RENDERER_OPENGL_ES, std::string_view{"OpenGL ES"}},
                                 std::pair{DIGITOR_RENDERER_VULKAN, std::string_view{"Vulkan"}}};
#else
    std::cerr << "QUALIFICATION SKIP reason=no native GPU backend for this host\n";
    return 77;
#endif
#if defined(_WIN32) || defined(__APPLE__) || defined(__ANDROID__)
    bool all_passed = true;
    unsigned executed = 0;
    for (const auto entry : entries) {
        auto backend = digitor::create_native_backend(entry.first);
        if (!backend || !backend->initialize(true)) {
            std::cerr << "BACKEND UNAVAILABLE backend=" << entry.second << '\n';
#if defined(_WIN32)
            if (entry.first == DIGITOR_RENDERER_D3D12) all_passed = false;
#endif
            continue;
        }
        ++executed;
        const auto info = backend->info();
        std::cerr << "DEVICE backend=" << entry.second << " identity=\"" << info.device_name << "\"\n";
        if(entry.first==DIGITOR_RENDERER_VULKAN){
          all_passed&=qualify_vulkan_cache_failure(*backend);
          all_passed&=qualify_vulkan_failure_matrix(*backend);
        }
        if(entry.first==DIGITOR_RENDERER_D3D12){
          all_passed&=qualify_d3d12_cache_failures(*backend);
          all_passed&=qualify_d3d12_failure_matrix(*backend);
        }
        if(entry.first==DIGITOR_RENDERER_METAL){
          all_passed&=qualify_metal_cache_failures(*backend);
          all_passed&=qualify_metal_failure_matrix(*backend);
        }
        if(entry.first==DIGITOR_RENDERER_OPENGL_ES){
          all_passed&=qualify_gles_cache_failures(*backend);
          all_passed&=qualify_gles_failure_matrix(*backend);
        }
        all_passed &= exercise(*backend, entry.second);
        const auto populated_cache=backend->native_pipeline_cache_counters();
        const bool cache_reused=populated_cache.lookups>=3&&populated_cache.misses>=2&&
            populated_cache.hits>=1&&populated_cache.creations>=2;
        std::cerr<<"NATIVE_CACHE backend="<<entry.second<<" lookups="<<populated_cache.lookups
                 <<" misses="<<populated_cache.misses<<" hits="<<populated_cache.hits
                 <<" creations="<<populated_cache.creations<<" evictions="<<populated_cache.evictions
                 <<" invalidations="<<populated_cache.invalidations
                 <<" creation_failures="<<populated_cache.creation_failures
                 <<" status="<<(cache_reused?"PASS":"FAIL")<<'\n';
        all_passed&=cache_reused;
        std::vector<digitor::Color> retirement_input(4,{.2f,.4f,.6f,1.f});
        auto retirement_parameters=digitor::PrimaryWheelsParameters::create();
        digitor::ProcessedGpuFramePtr retained;
        const auto retirement_produce=backend->process_primary_wheels_gpu(
            retirement_input,2,2,901,*retirement_parameters,retained);
        std::shared_ptr<digitor::PreviewConsumerDestination> native_consumer;
        const auto consumer_create=backend->create_preview_consumer(retained,native_consumer);
        const auto consumer_submit=native_consumer?native_consumer->submit(retained):DIGITOR_RESULT_INTERNAL_ERROR;
        const auto consumer_resubmit=native_consumer?native_consumer->submit(retained):DIGITOR_RESULT_INTERNAL_ERROR;
        const bool consumer_qualified=consumer_create==DIGITOR_RESULT_OK&&
            consumer_submit==DIGITOR_RESULT_OK&&consumer_resubmit==DIGITOR_RESULT_OK&&
            native_consumer->submission_count()==2&&
            backend->execution_provenance().normal_preview_readback_count==0;
        std::cerr<<"NATIVE_CONSUMER backend="<<entry.second
                 <<" submissions="<<(native_consumer?native_consumer->submission_count():0)
                 <<" processed_readback="<<backend->execution_provenance().normal_preview_readback_count
                 <<" status="<<(consumer_qualified?"PASS":"FAIL")<<'\n';
        all_passed&=consumer_qualified;
        digitor::set_gpu_failure_point(digitor::GpuFailurePoint::PreviewAcquisition);
        std::shared_ptr<digitor::PreviewConsumerDestination> failed_consumer;
        const bool acquisition_failure=backend->create_preview_consumer(retained,failed_consumer)!=DIGITOR_RESULT_OK&&!failed_consumer;
        digitor::set_gpu_failure_point(digitor::GpuFailurePoint::PreviewPresentation);
        const bool submission_failure=native_consumer&&
            native_consumer->submit(retained)!=DIGITOR_RESULT_OK&&
            native_consumer->submission_count()==2;
        digitor::set_gpu_failure_point(digitor::GpuFailurePoint::None);
        std::shared_ptr<digitor::PreviewConsumerDestination> recovery_consumer;
        const bool consumer_recovery=backend->create_preview_consumer(retained,recovery_consumer)==DIGITOR_RESULT_OK&&
            recovery_consumer&&recovery_consumer->submit(retained)==DIGITOR_RESULT_OK;
        std::cerr<<"CONSUMER_FAILURE_INJECTION backend="<<entry.second
                 <<" acquisition="<<acquisition_failure<<" submission="<<submission_failure
                 <<" recovery="<<consumer_recovery<<'\n';
        all_passed&=acquisition_failure&&submission_failure&&consumer_recovery;
        const void* retired_context=backend.get();
        if(native_consumer)native_consumer->retire();
        if(recovery_consumer)recovery_consumer->retire();
        native_consumer.reset();
        recovery_consumer.reset();
        failed_consumer.reset();
        backend->shutdown();
        const auto retired_cache=backend->native_pipeline_cache_counters();
        const bool cache_invalidated=retired_cache.invalidations>=populated_cache.creations;
        backend.reset();
        const bool retired=retirement_produce==DIGITOR_RESULT_OK&&retained&&
            !retained->ready()&&
            retained->acquire(retired_context,entry.first)==DIGITOR_RESULT_NOT_INITIALIZED;
        retained.reset();
        auto replacement=digitor::create_native_backend(entry.first);
        digitor::ProcessedGpuFramePtr replacement_frame;
        const bool recreated=replacement&&replacement->initialize(true)&&
            replacement->process_primary_wheels_gpu(retirement_input,2,2,902,
                *retirement_parameters,replacement_frame)==DIGITOR_RESULT_OK&&replacement_frame;
        const auto replacement_cache=replacement?replacement->native_pipeline_cache_counters():digitor::NativePipelineCacheCounters{};
        const bool replacement_miss=recreated&&replacement_cache.misses>=1&&replacement_cache.creations>=1;
        std::cerr<<"RETIREMENT backend="<<entry.second
                 <<" retained_frame_rejected="<<retired
                 <<" owner_release_safe="<<retired
                 <<" cache_invalidated="<<cache_invalidated
                 <<" replacement_cache_miss="<<replacement_miss
                 <<" replacement_execution="<<recreated<<'\n';
        all_passed&=retired&&recreated&&cache_invalidated&&replacement_miss;
        if(replacement)replacement->shutdown();
    }
    if (!executed) { std::cerr << "QUALIFICATION SKIP reason=no usable GPU device\n"; return 77; }
    return all_passed ? 0 : 1;
#endif
}
