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

bool exercise(digitor::IRenderBackend& backend, std::string_view name) {
    constexpr std::array dimensions{std::pair{1u,1u}, std::pair{2u,2u}, std::pair{3u,2u},
        std::pair{7u,5u}, std::pair{63u,17u}, std::pair{65u,3u}, std::pair{257u,2u}};
    constexpr std::array colors{Pixel{255,0,0,255}, Pixel{0,255,0,255}, Pixel{0,0,255,255},
        Pixel{0,0,0,255}, Pixel{17,53,101,211}};
    bool passed = true;
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
    std::cerr<<"RGB CURVES METRICS backend="<<name<<" max_error="<<curve_max<<" relative_error="<<curve_relative<<" rms="<<curve_rms<<" psnr="<<curve_psnr<<" ssim="<<curve_ssim<<" worst_pixel="<<worst<<'\n';
    const auto& curve_provenance=backend.execution_provenance();
    passed &= curve_result==DIGITOR_RESULT_OK && curve_max<2e-5 && curve_ssim>.99999 &&
      curve_provenance.dispatch_or_draw_issued && curve_provenance.validation_readback_completed &&
      curve_provenance.cpu_curve_invocations==0 && curve_provenance.curve_fallback_invocations==0;
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
    bool all_passed = true;
    bool required_backend_available = false;
#if defined(_WIN32)
    constexpr std::array backends{
        std::pair{DIGITOR_RENDERER_VULKAN, std::string_view{"Vulkan"}},
        std::pair{DIGITOR_RENDERER_D3D12, std::string_view{"Direct3D12"}}};
#else
    constexpr std::array backends{
        std::pair{DIGITOR_RENDERER_VULKAN, std::string_view{"Vulkan"}}};
#endif
    for (const auto entry : backends) {
        auto backend = digitor::create_native_backend(entry.first);
        if (!backend || !backend->initialize(true)) {
            std::cerr << "BACKEND UNAVAILABLE backend=" << entry.second << '\n';
            continue;
        }
#if defined(_WIN32)
        if (entry.first == DIGITOR_RENDERER_D3D12)
            required_backend_available = true;
#else
        required_backend_available = true;
#endif
        all_passed &= exercise(*backend, entry.second);
        backend->shutdown();
    }
#if defined(_WIN32)
    if (!required_backend_available) {
        std::cerr << "Direct3D12 is required for Windows qualification.\n";
        return 1;
    }
#else
    if (!required_backend_available) {
        std::cout << "No Vulkan device was executed; hardware remains unverified.\n";
        return 77;
    }
#endif
    return all_passed ? 0 : 1;
}
