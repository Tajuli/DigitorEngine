#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace digitor {

enum class DenoiseBackend : std::uint32_t { cpu = 0, vulkan = 1, d3d12 = 2, metal = 3, gles = 4 };
enum class DenoiseStatus : std::uint32_t { invalid = 0, ready = 1, backend_unavailable = 2, dispatch_failed = 3 };

struct DenoisePixel { float r{}, g{}, b{}, a{1.0f}; };
struct DenoiseFrame { std::uint32_t width{}, height{}; std::vector<DenoisePixel> pixels; };
struct DenoiseSettings {
  float spatial_strength{0.35f};
  float temporal_strength{0.45f};
  float luma_threshold{0.08f};
  float chroma_threshold{0.10f};
  float detail_preservation{0.65f};
  float motion_sensitivity{0.70f};
};
struct DenoiseResult { DenoiseStatus status{DenoiseStatus::invalid}; std::uint64_t digest{}; float average_delta{}; };
struct DenoiseDispatchPacket {
  DenoiseBackend backend{DenoiseBackend::cpu};
  std::uint32_t width{}, height{};
  std::uint64_t current_handle{}, previous_handle{}, motion_handle{}, output_handle{}, command_handle{};
  DenoiseSettings settings{};
};
using DenoiseDispatch = std::function<bool(const DenoiseDispatchPacket&)>;

DenoiseResult apply_video_denoise_reference(const DenoiseFrame& current,
                                            const DenoiseFrame* previous,
                                            const std::vector<float>* motion_confidence,
                                            DenoiseFrame& output,
                                            const DenoiseSettings& settings);
DenoiseResult dispatch_video_denoise_gpu(const DenoiseDispatchPacket& packet,
                                         const DenoiseDispatch& dispatch);
std::uint64_t denoise_frame_digest(const DenoiseFrame& frame) noexcept;

}  // namespace digitor

extern "C" {
struct DigitorVideoDenoiseSettings {
  float spatial_strength, temporal_strength, luma_threshold, chroma_threshold;
  float detail_preservation, motion_sensitivity;
};
std::uint32_t digitor_video_denoise_rgba32f(const float* current,
                                            const float* previous,
                                            const float* motion_confidence,
                                            float* output,
                                            std::uint32_t width,
                                            std::uint32_t height,
                                            const DigitorVideoDenoiseSettings* settings,
                                            std::uint64_t* digest);
}
