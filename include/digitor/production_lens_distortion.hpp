#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace digitor {
struct LensPixel { float r{}, g{}, b{}, a{1.0f}; };
struct LensFrame { std::uint32_t width{}, height{}; std::vector<LensPixel> pixels; };
enum class LensMode : std::uint32_t { distort=0, correct=1 };
enum class LensEdgeMode : std::uint32_t { transparent=0, clamp=1, mirror=2 };
enum class LensBackend : std::uint32_t { cpu=0, vulkan=1, d3d12=2, metal=3, gles=4 };
enum class LensStatus : std::uint32_t { invalid=0, ready=1, backend_unavailable=2, dispatch_failed=3 };
struct LensDistortionSettings {
  LensMode mode{LensMode::correct};
  LensEdgeMode edge_mode{LensEdgeMode::transparent};
  float k1{}, k2{}, k3{}, p1{}, p2{};
  float center_x{0.5f}, center_y{0.5f};
  float scale{1.0f};
  float aspect{1.0f};
  float chromatic_aberration{};
  bool high_quality{true};
};
struct LensResult { LensStatus status{LensStatus::invalid}; std::uint64_t digest{}; };
struct LensDispatchPacket {
  LensBackend backend{LensBackend::cpu};
  std::uint32_t width{}, height{};
  std::uint64_t input_handle{}, output_handle{}, command_handle{};
  LensDistortionSettings settings{};
};
using LensDispatch = std::function<bool(const LensDispatchPacket&)>;
LensResult apply_lens_distortion_reference(const LensFrame& input, LensFrame& output, const LensDistortionSettings& settings);
LensResult dispatch_lens_distortion_gpu(const LensDispatchPacket& packet, const LensDispatch& dispatch);
std::uint64_t lens_frame_digest(const LensFrame& frame) noexcept;
}

extern "C" {
struct DigitorLensDistortionSettings {
  std::uint32_t mode, edge_mode;
  float k1,k2,k3,p1,p2,center_x,center_y,scale,aspect,chromatic_aberration;
  std::uint32_t high_quality;
};
std::uint32_t digitor_lens_distortion_rgba32f(const float* in, float* out, std::uint32_t width,
                                              std::uint32_t height,
                                              const DigitorLensDistortionSettings* settings,
                                              std::uint64_t* digest);
}
