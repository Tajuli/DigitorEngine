#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace digitor {

enum class StabilizationBackend : std::uint32_t { cpu, vulkan, d3d12, metal, gles };
enum class StabilizationStatus : std::uint32_t { invalid, ready, backend_unavailable, dispatch_failed };

struct MotionSample {
  double timestamp{};
  float dx{};
  float dy{};
  float rotation{};
  float confidence{1.0f};
};

struct StabilizationSettings {
  float strength{0.75f};
  float smoothing{0.85f};
  float max_zoom{1.15f};
  bool lock_horizon{};
  bool rolling_shutter_correction{};
};

struct StabilizationTransform {
  float translate_x{};
  float translate_y{};
  float rotation{};
  float zoom{1.0f};
};

struct StabilizationPlan {
  std::vector<StabilizationTransform> transforms;
  std::uint64_t digest{};
};

struct StabilizationDispatchPacket {
  StabilizationBackend backend{StabilizationBackend::cpu};
  std::uint64_t input_handle{};
  std::uint64_t output_handle{};
  std::uint64_t command_handle{};
  std::uint32_t width{};
  std::uint32_t height{};
  StabilizationTransform transform{};
};

struct StabilizationResult {
  StabilizationStatus status{StabilizationStatus::invalid};
  std::uint64_t digest{};
};

using StabilizationDispatch = std::function<bool(const StabilizationDispatchPacket&)>;

StabilizationPlan build_stabilization_plan(const std::vector<MotionSample>& samples,
                                           const StabilizationSettings& settings);
StabilizationResult dispatch_stabilized_frame(const StabilizationDispatchPacket& packet,
                                              const StabilizationDispatch& dispatch);
std::uint64_t stabilization_plan_digest(const StabilizationPlan& plan) noexcept;

}  // namespace digitor

extern "C" {
struct DigitorMotionSample { double timestamp; float dx, dy, rotation, confidence; };
struct DigitorStabilizationSettings { float strength, smoothing, max_zoom; std::uint32_t lock_horizon, rolling_shutter_correction; };
struct DigitorStabilizationTransform { float translate_x, translate_y, rotation, zoom; };
std::uint32_t digitor_build_stabilization_plan(const DigitorMotionSample* samples,
                                               std::size_t sample_count,
                                               const DigitorStabilizationSettings* settings,
                                               DigitorStabilizationTransform* transforms,
                                               std::uint64_t* digest);
}
