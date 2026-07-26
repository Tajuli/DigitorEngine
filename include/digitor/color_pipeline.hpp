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
enum class ColorPass { primary, curves, hue_curves, wheels };
class ParameterBuffer { public: template<class T>void write(const T&v){auto*p=reinterpret_cast<const std::byte*>(&v);bytes_.assign(p,p+sizeof(T));} std::span<const std::byte> bytes()const{return bytes_;} private:std::vector<std::byte>bytes_; };
class ColorShaderGraph { public: ColorShaderGraph(); const std::vector<ColorPass>& schedule()const{return schedule_;} void process_cpu(std::span<const Color>,std::span<Color>,const ColorPipelineParameters&)const; void process_gpu(CommandEncoder&,std::span<const Color>,std::span<Color>,const ColorPipelineParameters&); std::size_t shader_cache_size()const{return shaders_.size();}std::size_t pipeline_cache_size()const{return pipelines_.size();} private:std::vector<ColorPass>schedule_;ShaderCompiler compiler_;ShaderCache shaders_;PipelineCache pipelines_;};
}
