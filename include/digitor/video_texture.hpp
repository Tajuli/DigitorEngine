#pragma once
#include "digitor/color.hpp"
#include "digitor/digitor.h"
#include "digitor/media.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace digitor {
struct ImagePlane { std::span<const std::uint8_t> bytes; std::size_t stride{}; };
struct DecodedImage { std::uint32_t width{}, height{}; PixelFormat format{PixelFormat::rgba8}; ColorRange range{ColorRange::limited}; ImagePlane planes[3]; };
// Backend-owned texture facade. Production backends use the same plane layout when
// filling their staging resources; rgba_reference is also retained for validation.
struct NativeVideoTexture { DigitorRendererBackend backend{DIGITOR_RENDERER_CPU}; std::uint32_t width{},height{}; PixelFormat source_format{}; std::vector<Color> rgba_reference; };
std::vector<Color> convert_to_linear_rgba(const DecodedImage&);
NativeVideoTexture upload_video_texture(DigitorRendererBackend, const DecodedImage&);
struct PixelValidation { float maximum_error{}, mean_error{}; std::size_t failing_pixels{}; bool passed{}; };
PixelValidation validate_pixels(std::span<const Color>, std::span<const Color>, float tolerance=1e-5f);
}
