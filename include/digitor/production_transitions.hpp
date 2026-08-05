#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace digitor {

enum class TransitionType : std::uint32_t {
  cross_dissolve = 0,
  dip_to_color = 1,
  wipe = 2,
  slide = 3,
};

enum class TransitionDirection : std::uint32_t {
  left = 0,
  right = 1,
  up = 2,
  down = 3,
};

enum class TransitionBackend : std::uint32_t {
  cpu = 0,
  vulkan = 1,
  d3d12 = 2,
  metal = 3,
  gles = 4,
};

enum class TransitionStatus : std::uint32_t {
  invalid = 0,
  ready = 1,
  backend_unavailable = 2,
  dispatch_failed = 3,
};

struct TransitionPixel {
  float r{};
  float g{};
  float b{};
  float a{1.0f};
};

struct TransitionFrame {
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<TransitionPixel> pixels;
};

struct TransitionSettings {
  TransitionType type{TransitionType::cross_dissolve};
  TransitionDirection direction{TransitionDirection::left};
  float progress{};
  float softness{0.02f};
  float dip_r{};
  float dip_g{};
  float dip_b{};
  float dip_a{1.0f};
  bool ease_in_out{true};
};

struct TransitionResult {
  TransitionStatus status{TransitionStatus::invalid};
  std::uint64_t digest{};
};

struct TransitionDispatchPacket {
  TransitionBackend backend{TransitionBackend::cpu};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint64_t input_a_handle{};
  std::uint64_t input_b_handle{};
  std::uint64_t output_handle{};
  std::uint64_t command_handle{};
  TransitionSettings settings;
};

using TransitionDispatch = std::function<bool(const TransitionDispatchPacket&)>;

std::uint64_t transition_frame_digest(const TransitionFrame& frame) noexcept;
TransitionResult apply_transition_reference(const TransitionFrame& a,
                                            const TransitionFrame& b,
                                            TransitionFrame& output,
                                            const TransitionSettings& settings);
TransitionResult dispatch_transition_gpu(const TransitionDispatchPacket& packet,
                                         const TransitionDispatch& dispatch);

}  // namespace digitor

extern "C" {
struct DigitorTransitionSettings {
  std::uint32_t type;
  std::uint32_t direction;
  float progress;
  float softness;
  float dip_r;
  float dip_g;
  float dip_b;
  float dip_a;
  std::uint32_t ease_in_out;
};

std::uint32_t digitor_transition_rgba32f(const float* input_a,
                                         const float* input_b,
                                         float* output,
                                         std::uint32_t width,
                                         std::uint32_t height,
                                         const DigitorTransitionSettings* settings,
                                         std::uint64_t* digest);
}
