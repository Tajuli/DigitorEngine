#pragma once
#include "digitor/editor_visual_features.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

struct PixelF { float r{}, g{}, b{}, a{}; };
struct FrameF {
  std::uint32_t width{}, height{};
  std::vector<PixelF> pixels;
  [[nodiscard]] bool valid() const noexcept { return width && height && pixels.size()==std::size_t(width)*height; }
};

enum class SamplingMode : std::uint32_t { nearest, bilinear };
enum class BlendMode : std::uint32_t { normal, add, multiply, screen };

struct MotionSample { std::int64_t frame{}; double x{}, y{}, rotation_degrees{}, scale{1.0}; };
struct AnimationKey { std::int64_t frame{}; double value{}; };
struct AnimatedScalar { double default_value{}; std::vector<AnimationKey> keys; };

struct SpatialSettings {
  TransformState transform;
  CropState crop;
  SamplingMode sampling{SamplingMode::bilinear};
};

struct LayerSettings {
  SpatialSettings spatial;
  ChromaKeyState chroma_key;
  BlendMode blend{BlendMode::normal};
  double opacity{1.0};
};

struct RenderPolicy {
  VisualBackend backend{VisualBackend::cpu};
  bool gpu_available{};
  bool allow_cpu_fallback{true};
  bool preview{};
};

struct RenderResult {
  FrameF frame;
  bool gpu_path_used{};
  std::string diagnostic;
};

double evaluate(const AnimatedScalar&, std::int64_t frame);
TransformState apply_stabilization(const TransformState&, const StabilizationState&,
                                   const std::vector<MotionSample>&, std::int64_t frame);
RenderResult render_spatial(const FrameF&, std::uint32_t output_width,
                            std::uint32_t output_height, const SpatialSettings&,
                            const RenderPolicy&);
void apply_chroma_key(FrameF&, const ChromaKeyState&);
void composite(FrameF& destination, const FrameF& foreground,
               BlendMode mode=BlendMode::normal, double opacity=1.0);
RenderResult render_layer(const FrameF& foreground, const FrameF& background,
                          const LayerSettings&, const StabilizationState&,
                          const std::vector<MotionSample>&, std::int64_t frame,
                          const RenderPolicy&);
std::string stable_frame_digest(const FrameF&);

} // namespace digitor
