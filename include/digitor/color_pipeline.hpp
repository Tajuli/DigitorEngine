#pragma once
#include "digitor/color.hpp"
#include "digitor/shader.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace digitor {
struct CurvePoint { float x{},y{}; };
class Curve { public: Curve(); explicit Curve(std::vector<CurvePoint>); float sample(float)const; const std::vector<CurvePoint>& points()const{return points_;} private:std::vector<CurvePoint>points_; };
struct ColorPipelineParameters {
 ColorGrade primary{}; Curve red,green,blue,master,hue_vs_hue,hue_vs_sat,hue_vs_lum,sat_vs_sat,lum_vs_sat;
 Color lift_wheel{},gamma_wheel{},gain_wheel{};
};
// The production graph is deliberately expressed at the same granularity and
// in the same order as grade_color().  Curves, wheels, qualifiers and LUTs are
// contracts for later milestones and are not native color stages in v4.6.
enum class ColorPass { temperature, tint, saturation, contrast, lift, gain,
 offset, exposure, gamma, hue };
enum class ColorPrecision { fp32 };
struct ColorValidationMetrics {
 float maximum_absolute_error{}, maximum_relative_error{}, rms{}, psnr{}, ssim{};
 std::size_t first_failing_pixel{static_cast<std::size_t>(-1)}, worst_pixel{};
 ShaderBackend backend{ShaderBackend::vulkan}; ColorPrecision precision{ColorPrecision::fp32};
 bool passed{};
};
class ParameterBuffer { public: template<class T>void write(const T&v){auto*p=reinterpret_cast<const std::byte*>(&v);bytes_.assign(p,p+sizeof(T));} std::span<const std::byte> bytes()const{return bytes_;} private:std::vector<std::byte>bytes_; };
class ColorShaderGraph { public: ColorShaderGraph(); const std::vector<ColorPass>& schedule()const{return schedule_;} void process_cpu(std::span<const Color>,std::span<Color>,const ColorPipelineParameters&)const; void process_gpu(CommandEncoder&,std::span<const Color>,std::span<Color>,const ColorPipelineParameters&); std::size_t shader_cache_size()const;std::size_t pipeline_cache_size()const; private:std::vector<ColorPass>schedule_; mutable ShaderCache shaders_; mutable PipelineCache pipelines_;};
ColorValidationMetrics validate_color_pipeline(std::span<const Color> reference,
 std::span<const Color> actual, ShaderBackend backend, float absolute_tolerance=2e-5f,
 float relative_tolerance=2e-5f);
}
