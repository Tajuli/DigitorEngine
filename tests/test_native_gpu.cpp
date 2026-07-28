#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <string_view>
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
  bool passed=true;for(const auto&test:cases){auto p=digitor::PrimaryWheelsParameters::create(test.p);std::vector<digitor::Color>expected(input.size()),actual(input.size());digitor::apply_primary_wheels_reference(input,expected,*p);digitor::ProcessedGpuFramePtr frame;auto result=backend.process_primary_wheels_gpu(input,width,height,101,*p,frame);auto present=frame?backend.present_gpu_frame(frame):DIGITOR_RESULT_INTERNAL_ERROR;auto read=frame?backend.validation_readback_primary_wheels(frame,actual):DIGITOR_RESULT_INTERNAL_ERROR;const auto&prov=backend.execution_provenance();double max_abs=0,max_rel=0,sum=0;size_t worst=0,first=SIZE_MAX,failures=0;for(size_t n=0;n<actual.size();++n)for(int c=0;c<4;c++){const float*e=&expected[n].r,*a=&actual[n].r;double error=std::abs(double(e[c])-a[c]),rel=error/std::max(1e-6,std::abs(double(e[c])));if(error>max_abs){max_abs=error;worst=n;}max_rel=std::max(max_rel,rel);sum+=error*error;if(error>2e-5&&rel>2e-5){if(first==SIZE_MAX)first=n;++failures;}}double rms=std::sqrt(sum/(actual.size()*4)),psnr=rms==0?INFINITY:20*std::log10(1/rms);digitor::VideoFrame ev{.width=width,.height=height,.pixels=expected},av{.width=width,.height=height,.pixels=actual};double ssim=digitor::calculate_ssim(ev,av);auto print=[](digitor::Color c){std::cerr<<'('<<c.r<<','<<c.g<<','<<c.b<<','<<c.a<<')';};std::cerr<<"PRIMARY_WHEELS_METRICS backend="<<name<<" device=\""<<backend.info().device_name<<"\" case="<<test.name<<" max_absolute_error="<<max_abs<<" max_relative_error="<<max_rel<<" rms="<<rms<<" psnr="<<psnr<<" ssim="<<ssim<<" failing_pixels="<<failures<<" first_failing_coordinate="<<(first==SIZE_MAX?-1:int(first%width))<<','<<(first==SIZE_MAX?-1:int(first/width))<<" worst_coordinate="<<worst%width<<','<<worst/width<<" expected=";print(expected[worst]);std::cerr<<" actual=";print(actual[worst]);std::cerr<<" cpu_delta="<<prov.cpu_primary_wheels_invocations<<" fallback="<<prov.primary_wheels_fallback_invocations<<" normal_readback="<<prov.normal_preview_readback_count<<'\n';passed&=result==DIGITOR_RESULT_OK&&present==DIGITOR_RESULT_OK&&read==DIGITOR_RESULT_OK&&frame&&frame->identity()!=0&&prov.preview_source==digitor::PreviewSource::gpu&&prov.direct_preview_consumed&&prov.cpu_primary_wheels_invocations==0&&prov.primary_wheels_fallback_invocations==0&&prov.normal_preview_readback_count==0&&prov.validation_readback_completed&&prov.dispatch_or_draw_issued&&prov.queue_submission_issued&&prov.synchronization_waited&&failures==0&&rms<=5e-6&&ssim>=.99999;}
  for(auto point:{digitor::GpuFailurePoint::PipelineCreation,digitor::GpuFailurePoint::DestinationAllocation,digitor::GpuFailurePoint::DescriptorAllocation,digitor::GpuFailurePoint::CommandRecording,digitor::GpuFailurePoint::QueueSubmission,digitor::GpuFailurePoint::Synchronization}){digitor::set_gpu_failure_point(point);auto p=digitor::PrimaryWheelsParameters::create();digitor::ProcessedGpuFramePtr failed;auto r=backend.process_primary_wheels_gpu(input,width,height,102,*p,failed);passed&=r!=DIGITOR_RESULT_OK&&!failed&&backend.execution_provenance().cpu_primary_wheels_invocations==0&&backend.execution_provenance().primary_wheels_fallback_invocations==0&&backend.execution_provenance().normal_preview_readback_count==0;}digitor::set_gpu_failure_point(digitor::GpuFailurePoint::None);auto p=digitor::PrimaryWheelsParameters::create();digitor::ProcessedGpuFramePtr preview;passed&=backend.process_primary_wheels_gpu(input,width,height,103,*p,preview)==DIGITOR_RESULT_OK&&preview;for(auto point:{digitor::GpuFailurePoint::PreviewAcquisition,digitor::GpuFailurePoint::PreviewPresentation}){digitor::set_gpu_failure_point(point);passed&=backend.present_gpu_frame(preview)!=DIGITOR_RESULT_OK&&backend.execution_provenance().cpu_primary_wheels_invocations==0&&backend.execution_provenance().primary_wheels_fallback_invocations==0&&backend.execution_provenance().normal_preview_readback_count==0;}digitor::set_gpu_failure_point(digitor::GpuFailurePoint::None);return passed;
}

bool exercise(digitor::IRenderBackend& backend, std::string_view name) {
    constexpr std::array dimensions{std::pair{1u,1u}, std::pair{2u,2u}, std::pair{3u,2u},
        std::pair{7u,5u}, std::pair{63u,17u}, std::pair{65u,3u}, std::pair{257u,2u}};
    constexpr std::array colors{Pixel{255,0,0,255}, Pixel{0,255,0,255}, Pixel{0,0,255,255},
        Pixel{0,0,0,255}, Pixel{17,53,101,211}};
    bool passed = true;
    passed &= qualify_primary_wheels(backend,name);
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
    for(const auto point:{digitor::GpuFailurePoint::DestinationAllocation,
                          digitor::GpuFailurePoint::DescriptorAllocation,
                          digitor::GpuFailurePoint::CommandRecording,
                          digitor::GpuFailurePoint::QueueSubmission,
                          digitor::GpuFailurePoint::Synchronization}){
      digitor::set_gpu_failure_point(point);digitor::ProcessedGpuFramePtr failed=processed;
      const auto failure=backend.process_curves_gpu(grade_input,3,1,74,*curves,failed);
      passed &= failure!=DIGITOR_RESULT_OK && !failed &&
        backend.execution_provenance().curve_fallback_invocations==0 &&
        !backend.execution_provenance().readback_performed;
    }
    for(const auto point:{digitor::GpuFailurePoint::PreviewAcquisition,
                          digitor::GpuFailurePoint::PreviewPresentation}){
      digitor::set_gpu_failure_point(point);
      passed &= backend.present_gpu_frame(processed)!=DIGITOR_RESULT_OK &&
        !backend.execution_provenance().readback_performed;
    }
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
        all_passed &= exercise(*backend, entry.second);
        backend->shutdown();
    }
    if (!executed) { std::cerr << "QUALIFICATION SKIP reason=no usable GPU device\n"; return 77; }
    return all_passed ? 0 : 1;
#endif
}
