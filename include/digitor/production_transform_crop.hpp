#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace digitor {

enum class TransformCropBackend : std::uint32_t { cpu, vulkan, d3d12, metal, gles };
enum class TransformCropStatus : std::uint32_t { invalid, ready, backend_unavailable, dispatch_failed };
enum class TransformCropFilter : std::uint32_t { nearest, bilinear };
enum class TransformCropEdge : std::uint32_t { transparent, clamp, mirror };

struct TransformCropPixel final { float r{}, g{}, b{}, a{1.0f}; };
struct TransformCropFrame final {
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<TransformCropPixel> pixels;
};

struct TransformCropSettings final {
  float matrix[9]{1.0f,0.0f,0.0f, 0.0f,1.0f,0.0f, 0.0f,0.0f,1.0f};
  float crop_left{};
  float crop_top{};
  float crop_right{1.0f};
  float crop_bottom{1.0f};
  std::uint32_t output_width{};
  std::uint32_t output_height{};
  TransformCropFilter filter{TransformCropFilter::bilinear};
  TransformCropEdge edge{TransformCropEdge::transparent};
};

struct TransformCropResult final {
  TransformCropStatus status{TransformCropStatus::invalid};
  std::uint64_t digest{};
};

struct TransformCropDispatchPacket final {
  TransformCropBackend backend{TransformCropBackend::cpu};
  std::uint64_t input_handle{};
  std::uint64_t output_handle{};
  std::uint64_t command_handle{};
  std::uint32_t input_width{};
  std::uint32_t input_height{};
  TransformCropSettings settings;
};

using TransformCropDispatch = std::function<bool(const TransformCropDispatchPacket&)>;

std::uint64_t transform_crop_digest(const TransformCropFrame&) noexcept;
TransformCropResult apply_transform_crop_reference(const TransformCropFrame& input,
                                                   TransformCropFrame& output,
                                                   const TransformCropSettings& settings);
TransformCropResult dispatch_transform_crop_gpu(const TransformCropDispatchPacket& packet,
                                                const TransformCropDispatch& dispatch);

}  // namespace digitor

extern "C" {
struct DigitorTransformCropSettings {
  float matrix[9];
  float crop_left, crop_top, crop_right, crop_bottom;
  std::uint32_t output_width, output_height, filter, edge;
};
std::uint32_t digitor_transform_crop_rgba32f(const float* input,
                                             std::uint32_t input_width,
                                             std::uint32_t input_height,
                                             float* output,
                                             const DigitorTransformCropSettings* settings,
                                             std::uint64_t* digest);
}
