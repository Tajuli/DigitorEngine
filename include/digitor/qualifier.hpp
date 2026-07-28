#pragma once
#include "digitor/color.hpp"
#include <cstdint>
#include <span>
#include <utility>
#include <vector>
namespace digitor {
struct QualifierRange {float low{},high{1},softness{};};
struct QualifierSettings {QualifierRange hue,saturation,luminance;float blur{},denoise{},clean_black{},clean_white{};bool invert{},matte_output{};};
class HslQualifier {public:void sample(Color);void sample(std::span<const Color>);const QualifierSettings&settings()const{return settings_;}void set_settings(QualifierSettings s){settings_=s;}std::vector<float> matte_cpu(std::span<const Color>,uint32_t,uint32_t)const;void matte_gpu(CommandEncoder&,std::span<const Color>,std::span<float>,uint32_t,uint32_t)const;private:QualifierSettings settings_{};};
}
