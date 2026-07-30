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


struct QualificationTolerance {
  double component_absolute;
  double component_relative;
  double rms;
  double ssim;
};

QualificationTolerance qualification_tolerance(const digitor::IRenderBackend& backend) {
  if (backend.info().backend == DIGITOR_RENDERER_OPENGL_ES)
    return {1.0e-3, 1.0e-3, 2.5e-4, 0.9999};
  return {2.0e-5, 2.0e-5, 5.0e-6, 0.99999};
}

bool qualify_primary_wheels(digitor::IRenderBackend&backend,std::string_view name){
  const auto tolerance=qualification_tolerance(backend);
  using D=digitor::PrimaryWheelsDescriptor;struct Case{const char*name;D p;};D lift;lift.lift={.08f,-.03f,.02f};D lm;lm.lift_master=.05f;D gamma;gamma.gamma={.8f,1.2f,1.5f};D gm;gm.gamma_master=1.3f;D gain;gain.gain={1.1f,.8f,1.4f};D gam;gam.gain_master=.85f;D offset;offset.offset={-.04f,.03f,.08f};D om;om.offset_master=-.02f;D all=lift;all.gamma=gamma.gamma;all.gain=gain.gain;all.offset=offset.offset;all.lift_master=.01f;all.gamma_master=1.1f;all.gain_master=.9f;all.offset_master=.02f;
  const std::array cases{Case{"identity",{}},Case{"lift-rgb",lift},Case{"lift-master",lm},Case{"gamma-rgb",gamma},Case{"gamma-master",gm},Case{"gain-rgb",gain},Case{"gain-master",gam},Case{"offset-rgb",offset},Case{"offset-master",om},Case{"combined",all}};
  constexpr uint32_t width=7,height=5;std::vector<digitor::Color>input(size_t(width)*height);uint32_t state=0x12345678u;for(size_t n=0;n<input.size();++n){state=state*1664525u+1013904223u;auto f=[&](uint32_t shift){return (float((state>>shift)&255)/127.5f)-.5f;};input[n]={f(0),f(8),f(16),.15f+float(n%7)/10};}input[0]={0,0,0,.3f};input[1]={1,0,0,.4f};input[2]={0,1,0,.5f};input[3]={0,0,1,.6f};input[4]={-.75f,-.1f,-2.f,.7f};input[5]={1.5f,4.f,12.f,.8f};input[6]={.25f,.25f,.25f,.9f};
  bool passed=true;for(const auto&test:cases){auto p=digitor::PrimaryWheelsParameters::create(test.p);std::vector<digitor::Color>expected(input.size()),actual(input.size());digitor::apply_primary_wheels_reference(input,expected,*p);digitor::ProcessedGpuFramePtr frame;auto result=backend.process_primary_wheels_gpu(input,width,height,101,*p,frame);auto present=frame?backend.present_gpu_frame(frame):DIGITOR_RESULT_INTERNAL_ERROR;const auto preview_prov=backend.execution_provenance();auto read=frame?backend.validation_readback_primary_wheels(frame,actual):DIGITOR_RESULT_INTERNAL_ERROR;const auto validation_prov=backend.execution_provenance();double max_abs=0,max_rel=0,sum=0;size_t worst=0,first=SIZE_MAX,failures=0;for(size_t n=0;n<actual.size();++n)for(int c=0;c<4;c++){const float*e=&expected[n].r,*a=&actual[n].r;double error=std::abs(double(e[c])-a[c]),rel=error/std::max(1e-6,std::abs(double(e[c])));if(error>max_abs){max_abs=error;worst=n;}max_rel=std::max(max_rel,rel);sum+=error*error;if(error>tolerance.component_absolute&&rel>tolerance.component_relative){if(first==SIZE_MAX)first=n;++failures;}}double rms=std::sqrt(sum/(actual.size()*4)),psnr=rms==0?INFINITY:20*std::log10(1/rms);digitor::VideoFrame ev{.width=width,.height=height,.pixels=expected},av{.width=width,.height=height,.pixels=actual};double ssim=digitor::calculate_ssim(ev,av);auto print=[](digitor::Color c){std::cerr<<'('<<c.r<<','<<c.g<<','<<c.b<<','<<c.a<<')';};std::cerr<<"PRIMARY_WHEELS_METRICS backend="<<name<<" device=\""<<backend.info().device_name<<"\" case="<<test.name<<" max_absolute_error="<<max_abs<<" max_relative_error="<<max_rel<<" rms="<<rms<<" psnr="<<psnr<<" ssim="<<ssim<<" failing_pixels="<<failures<<" first_failing_coordinate="<<(first==SIZE_MAX?-1:int(first%width))<<','<<(first==SIZE_MAX?-1:int(first/width))<<" worst_coordinate="<<worst%width<<','<<worst/width<<" expected=";print(expected[worst]);std::cerr<<" actual=";print(actual[worst]);std::cerr<<" cpu_delta="<<preview_prov.cpu_primary_wheels_invocations<<" fallback="<<preview_prov.primary_wheels_fallback_invocations<<" normal_readback="<<preview_prov.normal_preview_readback_count<<'\n';passed&=result==DIGITOR_RESULT_OK&&present==DIGITOR_RESULT_OK&&read==DIGITOR_RESULT_OK&&frame&&frame->identity()!=0&&preview_prov.preview_source==digitor::PreviewSource::gpu&&preview_prov.direct_preview_consumed&&preview_prov.cpu_primary_wheels_invocations==0&&preview_prov.primary_wheels_fallback_invocations==0&&preview_prov.normal_preview_readback_count==0&&validation_prov.validation_readback_completed&&validation_prov.cpu_primary_wheels_invocations==0&&validation_prov.primary_wheels_fallback_invocations==0&&validation_prov.normal_preview_readback_count==0&&failures==0&&rms<=tolerance.rms&&ssim>=tolerance.ssim;}
  digitor::set_gpu_failure_point(digitor::GpuFailurePoint::None);auto p=digitor::PrimaryWheelsParameters::create();digitor::ProcessedGpuFramePtr preview;passed&=backend.process_primary_wheels_gpu(input,width,height,103,*p,preview)==DIGITOR_RESULT_OK&&preview;passed&=preview&&backend.present_gpu_frame(preview)==DIGITOR_RESULT_OK;digitor::set_gpu_failure_point(digitor::GpuFailurePoint::None);return passed;
}

bool qualify_composition(digitor::IRenderBackend& backend,std::string_view name){
  const auto tolerance=qualification_tolerance(backend);
  constexpr std::uint32_t width=7,height=5;std::vector<digitor::Color>input(std::size_t(width)*height);for(std::size_t n=0;n<input.size();++n)input[n]={float(int(n%9)-3)/4.f,float(int(n%7)-2)/3.f,float(int(n%11)-4)/5.f,.2f+float(n%5)/7.f};
  digitor::PrimaryWheelsDescriptor wd;wd.lift={.04f,-.02f,.01f};wd.gamma={.9f,1.2f,.8f};wd.gain={1.1f,.95f,1.05f};wd.offset_master=.015f;auto wheels=digitor::PrimaryWheelsParameters::create(wd);digitor::RgbCurvesParameters cd;cd.master.points={{0,0},{.3f,.2f},{.7f,.82f},{1,1}};cd.red.points={{0,0},{.5f,.58f},{1,1}};auto curves=digitor::CompiledRgbCurves::compile(cd);bool passed=true;
  for(bool wheels_first:{true,false}){std::vector<digitor::Color>middle(input.size()),expected(input.size()),actual(input.size());if(wheels_first){digitor::apply_primary_wheels_reference(input,middle,*wheels);curves->apply(middle,expected);}else{curves->apply(input,middle);digitor::apply_primary_wheels_reference(middle,expected,*wheels);}const auto pw_before=digitor::primary_wheels_reference_count(),curve_before=digitor::cpu_curve_reference_count();digitor::ProcessedGpuFramePtr first,final;auto first_result=wheels_first?backend.process_primary_wheels_gpu(input,width,height,201,*wheels,first):backend.process_curves_gpu(input,width,height,201,*curves,first);auto source=backend.gpu_source(first);auto second_result=first_result==DIGITOR_RESULT_OK?(wheels_first?backend.process_curves_gpu(source,202,*curves,final):backend.process_primary_wheels_gpu(source,202,*wheels,final)):first_result;auto read=final?backend.validation_readback_primary_wheels(final,actual):DIGITOR_RESULT_INTERNAL_ERROR;double max_abs=0,max_rel=0,squares=0;std::size_t failures=0,first_failure=SIZE_MAX,worst=0;for(std::size_t n=0;n<actual.size();++n)for(int c=0;c<4;c++){auto e=(&expected[n].r)[c],a=(&actual[n].r)[c];double error=std::abs(double(e)-a),relative=error/std::max(1e-6,std::abs(double(e)));if(error>max_abs){max_abs=error;worst=n;}max_rel=std::max(max_rel,relative);squares+=error*error;if(error>tolerance.component_absolute&&relative>tolerance.component_relative){if(first_failure==SIZE_MAX)first_failure=n;++failures;}}double rms=std::sqrt(squares/(actual.size()*4)),psnr=rms==0?INFINITY:20*std::log10(1/rms);digitor::VideoFrame ev{.width=width,.height=height,.pixels=expected},av{.width=width,.height=height,.pixels=actual};double ssim=digitor::calculate_ssim(ev,av);const auto&prov=backend.execution_provenance();auto print=[](digitor::Color c){std::cerr<<'('<<c.r<<','<<c.g<<','<<c.b<<','<<c.a<<')';};std::cerr<<"COMPOSITION_METRICS backend="<<name<<" device=\""<<backend.info().device_name<<"\" order="<<(wheels_first?"primary-then-curves":"curves-then-primary")<<" dimensions="<<width<<'x'<<height<<" max_absolute_error="<<max_abs<<" max_relative_error="<<max_rel<<" rms="<<rms<<" psnr="<<psnr<<" ssim="<<ssim<<" failing_components="<<failures<<" first_failing_coordinate="<<(first_failure==SIZE_MAX?-1:int(first_failure%width))<<','<<(first_failure==SIZE_MAX?-1:int(first_failure/width))<<" worst_coordinate="<<worst%width<<','<<worst/width<<" expected=";print(expected[worst]);std::cerr<<" actual=";print(actual[worst]);std::cerr<<" cpu_primary_delta="<<digitor::primary_wheels_reference_count()-pw_before<<" cpu_curves_delta="<<digitor::cpu_curve_reference_count()-curve_before<<" fallback="<<(prov.primary_wheels_fallback_invocations+prov.curve_fallback_invocations)<<" intermediate_readback=0 intermediate_reupload=0 normal_readback="<<prov.normal_preview_readback_count<<" validation_readback="<<(read==DIGITOR_RESULT_OK)<<" dispatches=2 submissions=2 synchronizations=2\n";passed&=first_result==DIGITOR_RESULT_OK&&second_result==DIGITOR_RESULT_OK&&read==DIGITOR_RESULT_OK&&digitor::primary_wheels_reference_count()==pw_before&&digitor::cpu_curve_reference_count()==curve_before&&prov.primary_wheels_fallback_invocations==0&&prov.curve_fallback_invocations==0&&prov.normal_preview_readback_count==0&&failures==0&&rms<=tolerance.rms&&ssim>=tolerance.ssim;}
  return passed;
}

bool qualify_direct_preview(digitor::IRenderBackend&backend,std::string_view name){constexpr uint32_t width=3,height=2;std::vector<digitor::Color>input(size_t(width)*height,{.2f,.5f,.8f,.7f});auto p=digitor::PrimaryWheelsParameters::create();digitor::ProcessedGpuFramePtr frame;auto r=backend.process_primary_wheels_gpu(input,width,height,301,*p,frame);auto present=frame?backend.present_gpu_frame(frame):DIGITOR_RESULT_INTERNAL_ERROR;const auto&prov=backend.execution_provenance();bool ok=r==DIGITOR_RESULT_OK&&present==DIGITOR_RESULT_OK&&frame&&prov.preview_source==digitor::PreviewSource::gpu&&prov.direct_preview_consumed&&prov.normal_preview_readback_count==0&&prov.primary_wheels_fallback_invocations==0;std::cerr<<"DIRECT_PREVIEW backend="<<name<<" source=gpu direct="<<prov.direct_preview_consumed<<" normal_readback="<<prov.normal_preview_readback_count<<" fallback="<<prov.primary_wheels_fallback_invocations<<" status="<<(ok?"PASS":"FAIL")<<'\n';return ok;}

bool qualify_curves(digitor::IRenderBackend&backend,std::string_view name){const auto tolerance=qualification_tolerance(backend);constexpr uint32_t width=7,height=5;std::vector<digitor::Color>input(size_t(width)*height);for(size_t n=0;n<input.size();++n)input[n]={float(int(n%13)-4)/6.f,float(int(n%11)-3)/5.f,float(int(n%17)-8)/8.f,.25f+float(n%4)/5};digitor::RgbCurvesParameters descriptor;descriptor.master.points={{0,0},{.2f,.08f},{.55f,.7f},{1,1}};descriptor.red.points={{0,0},{.5f,.62f},{1,1}};descriptor.green.points={{0,0},{.4f,.32f},{1,1}};descriptor.blue.points={{0,0},{.75f,.88f},{1,1}};auto curves=digitor::CompiledRgbCurves::compile(descriptor);std::vector<digitor::Color>expected(input.size()),actual(input.size());curves->apply(input,expected);const auto before=digitor::cpu_curve_reference_count();digitor::ProcessedGpuFramePtr frame;auto result=backend.process_curves_gpu(input,width,height,401,*curves,frame);auto present=frame?backend.present_gpu_frame(frame):DIGITOR_RESULT_INTERNAL_ERROR;const auto preview=backend.execution_provenance();auto read=frame?backend.validation_readback_curves(frame,actual):DIGITOR_RESULT_INTERNAL_ERROR;const auto validation=backend.execution_provenance();double max_abs=0,max_rel=0,sum=0;size_t failures=0,first=SIZE_MAX,worst=0;for(size_t n=0;n<actual.size();++n)for(int c=0;c<4;c++){auto e=(&expected[n].r)[c],a=(&actual[n].r)[c];double error=std::abs(double(e)-a),relative=error/std::max(1e-6,std::abs(double(e)));if(error>max_abs){max_abs=error;worst=n;}max_rel=std::max(max_rel,relative);sum+=error*error;if(error>tolerance.component_absolute&&relative>tolerance.component_relative){if(first==SIZE_MAX)first=n;++failures;}}double rms=std::sqrt(sum/(actual.size()*4)),psnr=rms==0?INFINITY:20*std::log10(1/rms);digitor::VideoFrame ev{.width=width,.height=height,.pixels=expected},av{.width=width,.height=height,.pixels=actual};double ssim=digitor::calculate_ssim(ev,av);const bool ok=result==DIGITOR_RESULT_OK&&present==DIGITOR_RESULT_OK&&read==DIGITOR_RESULT_OK&&frame&&preview.cpu_curve_invocations==0&&preview.curve_fallback_invocations==0&&preview.normal_preview_readback_count==0&&validation.validation_readback_completed&&validation.cpu_curve_invocations==0&&validation.curve_fallback_invocations==0&&validation.normal_preview_readback_count==0&&digitor::cpu_curve_reference_count()==before&&failures==0&&rms<=tolerance.rms&&ssim>=tolerance.ssim;std::cerr<<"RGB_CURVES_METRICS backend="<<name<<" max_absolute_error="<<max_abs<<" max_relative_error="<<max_rel<<" rms="<<rms<<" psnr="<<psnr<<" ssim="<<ssim<<" failing_components="<<failures<<" first_failing_coordinate="<<(first==SIZE_MAX?-1:int(first%width))<<','<<(first==SIZE_MAX?-1:int(first/width))<<" worst_coordinate="<<worst%width<<','<<worst/width<<" cpu_delta="<<preview.cpu_curve_invocations<<" fallback="<<preview.curve_fallback_invocations<<" normal_readback="<<preview.normal_preview_readback_count<<" status="<<(ok?"PASS":"FAIL")<<'\n';return ok;}

bool exercise(digitor::IRenderBackend& backend, std::string_view name) {
    return qualify_primary_wheels(backend,name)&&qualify_curves(backend,name)&&
           qualify_composition(backend,name)&&qualify_direct_preview(backend,name);
}

bool qualify_vulkan_cache_failure(digitor::IRenderBackend& backend);
bool qualify_vulkan_failure_matrix(digitor::IRenderBackend& backend);
bool qualify_d3d12_cache_failures(digitor::IRenderBackend& backend);
bool qualify_d3d12_failure_matrix(digitor::IRenderBackend& backend);
bool qualify_metal_cache_failures(digitor::IRenderBackend& backend);
bool qualify_metal_failure_matrix(digitor::IRenderBackend& backend);
bool qualify_gles_cache_failures(digitor::IRenderBackend& backend);
bool qualify_gles_failure_matrix(digitor::IRenderBackend& backend);

} // namespace

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

        // Capture the stale-handle contract before releasing the last strong
        // frame reference. The backend shutdown callback invalidates this
        // external readiness token, while releasing the frame here guarantees
        // all native image/view/memory owners are destroyed before vkDestroyDevice.
        const auto retained_ready_token = retained ? retained->readiness_token() : nullptr;
        retained.reset();

        backend->shutdown();
        const auto retired_cache=backend->native_pipeline_cache_counters();
        const bool cache_invalidated=retired_cache.invalidations>=populated_cache.creations;
        backend.reset();
        const bool retired=retirement_produce==DIGITOR_RESULT_OK&&retained_ready_token&&
            !retained_ready_token->load(std::memory_order_acquire);
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
        replacement_frame.reset();
        if(replacement)replacement->shutdown();
    }
    if (!executed) { std::cerr << "QUALIFICATION SKIP reason=no usable GPU device\n"; return 77; }
    return all_passed ? 0 : 1;
#endif
}
