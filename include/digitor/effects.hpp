#pragma once
#include "digitor/color.hpp"
#include <cstdint>

namespace digitor {
enum class EffectType { blur, sharpen, glow, lens_distortion, noise, film_grain, chromatic_aberration, vignette, motion_blur };
struct EffectSettings { EffectType type{EffectType::blur}; float amount{1}; float radius{1}; float angle{}; std::uint64_t seed{}; };
// Records a deterministic compute operation. Input and output may alias.
void apply_effect_gpu(CommandEncoder&, const Color* input, Color* output, std::uint32_t width, std::uint32_t height, const EffectSettings&);
}
