#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace digitor {

enum class MaskStatus : std::uint32_t { invalid, ready, backend_unavailable, dispatch_failed };
enum class MaskShape : std::uint32_t { rectangle, ellipse, polygon };
enum class MaskCombine : std::uint32_t { replace, add, subtract, intersect };
enum class MaskBackend : std::uint32_t { cpu, vulkan, d3d12, metal, gles };

struct MaskPoint { float x{}, y{}; };
struct MaskTransform { float translate_x{}, translate_y{}, scale_x{1.0f}, scale_y{1.0f}, rotation_degrees{}; };
struct MaskDefinition {
  MaskShape shape{MaskShape::rectangle};
  MaskCombine combine{MaskCombine::replace};
  std::vector<MaskPoint> points;
  MaskTransform transform;
  float feather{};
  float expansion{};
  float opacity{1.0f};
  bool invert{};
};
struct MaskFrame { std::uint32_t width{}, height{}; std::vector<float> alpha; };
struct MaskResult { MaskStatus status{MaskStatus::invalid}; std::uint64_t digest{}; float coverage{}; };
struct MaskDispatchPacket {
  MaskBackend backend{MaskBackend::cpu};
  std::uint32_t width{}, height{}, mask_count{};
  std::uint64_t output_handle{}, command_handle{}, definitions_handle{};
};
using MaskDispatch = std::function<bool(const MaskDispatchPacket&)>;

MaskResult render_masks_reference(std::uint32_t width, std::uint32_t height,
                                  const std::vector<MaskDefinition>& masks,
                                  MaskFrame& output);
MaskResult dispatch_masks_gpu(const MaskDispatchPacket& packet, const MaskDispatch& dispatch);
std::uint64_t mask_frame_digest(const MaskFrame& frame) noexcept;

}  // namespace digitor

extern "C" {
struct DigitorMaskPoint { float x, y; };
struct DigitorMaskDefinition {
  std::uint32_t shape, combine, point_offset, point_count;
  float translate_x, translate_y, scale_x, scale_y, rotation_degrees;
  float feather, expansion, opacity;
  std::uint32_t invert;
};
std::uint32_t digitor_render_masks_f32(std::uint32_t width, std::uint32_t height,
                                      const DigitorMaskDefinition* masks, std::uint32_t mask_count,
                                      const DigitorMaskPoint* points, std::uint32_t point_count,
                                      float* output_alpha, std::uint64_t* digest);
}
