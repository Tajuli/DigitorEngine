#pragma once
#include "digitor/commands.hpp"
#include <cstddef>
namespace digitor {
struct Color {float r{},g{},b{},a{1};};
struct ColorGrade {float exposure{},contrast{1},gamma{1},lift{},gain{1},offset{},temperature{},tint{},saturation{1},vibrance{},hue{};};
Color linearize_srgb(Color);Color encode_srgb(Color);Color grade_color(Color,const ColorGrade&);void grade_image_cpu(const Color*,Color*,size_t,const ColorGrade&);void grade_image_gpu(CommandEncoder&,const Color*,Color*,size_t,const ColorGrade&);
}
