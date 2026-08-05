#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace digitor {

enum class CompositeBackend : std::uint32_t { cpu, vulkan, d3d12, metal, gles };
enum class CompositeStatus : std::uint32_t { invalid, ready, backend_unavailable, dispatch_failed };
enum class BlendMode : std::uint32_t {
  normal,
  multiply,
  screen,
  overlay,
  darken,
  lighten,
  add,
  subtract,
  difference
};
enum class CompositeAlphaMode : std::uint32_t { straight, premultiplied };

struct CompositePixel final {
  float r{};
  float g{};
  float b{};
  float a{1.0f};
};

struct CompositeFrame final {
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<CompositePixel> pixels;
};

struct CompositeSettings final {
  BlendMode blend_mode{BlendMode::normal};
  CompositeAlphaMode input_alpha{CompositeAlphaMode::straight};
  CompositeAlphaMode output_alpha{CompositeAlphaMode::straight};
  float opacity{1.0f};
};

struct CompositeResult final {
  CompositeStatus status{CompositeStatus::invalid};
  std::uint64_t digest{};
};

struct CompositeDispatchPacket final {
  CompositeBackend backend{CompositeBackend::cpu};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint64_t background_handle{};
  std::uint64_t foreground_handle{};
  std::uint64_t output_handle{};
  std::uint64_t command_handle{};
  CompositeSettings settings;
};

using CompositeDispatch = std::function<bool(const CompositeDispatchPacket&)>;

std::uint64_t composite_frame_digest(const CompositeFrame& frame) noexcept;
CompositeResult composite_reference(const CompositeFrame& background,
                                    const CompositeFrame& foreground,
                                    CompositeFrame& output,
                                    const CompositeSettings& settings);
CompositeResult dispatch_composite_gpu(const CompositeDispatchPacket& packet,
                                       const CompositeDispatch& dispatch);

}  // namespace digitor

extern "C" {
struct DigitorCompositeSettings {
  std::uint32_t blend_mode;
  std::uint32_t input_alpha;
  std::uint32_t output_alpha;
  float opacity;
};

std::uint32_t digitor_composite_rgba32f(const float* background,
                                        const float* foreground,
                                        float* output,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        const DigitorCompositeSettings* settings,
                                        std::uint64_t* digest);
}
