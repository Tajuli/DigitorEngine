#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace digitor {

struct ChromaPixel { float r{}, g{}, b{}, a{1.0f}; };
struct ChromaFrame { std::uint32_t width{}, height{}; std::vector<ChromaPixel> pixels; };

enum class ChromaKeyColor : std::uint32_t { green = 0, blue = 1, custom = 2 };
enum class ChromaBackend : std::uint32_t { cpu = 0, vulkan = 1, d3d12 = 2, metal = 3, gles = 4 };
enum class ChromaStatus : std::uint32_t { ready = 0, invalid_argument, backend_unavailable, dispatch_failed };

struct ChromaKeySettings {
  ChromaKeyColor key_mode{ChromaKeyColor::green};
  float key_r{0.0f}, key_g{1.0f}, key_b{0.0f};
  float similarity{0.35f};
  float softness{0.12f};
  float edge_shrink{0.0f};
  float edge_blur{0.0f};
  float despill{0.65f};
  float spill_balance{0.5f};
  bool invert_matte{false};
};

struct ChromaDispatchPacket {
  ChromaBackend backend{ChromaBackend::cpu};
  std::uint64_t input_handle{};
  std::uint64_t output_handle{};
  std::uint64_t command_handle{};
  std::uint32_t width{}, height{};
  ChromaKeySettings settings{};
};

struct ChromaResult {
  ChromaStatus status{ChromaStatus::invalid_argument};
  std::uint64_t digest{};
  float transparent_ratio{};
};

using ChromaDispatch = std::function<bool(const ChromaDispatchPacket&)>;

ChromaResult apply_chroma_key_reference(const ChromaFrame& input,
                                        ChromaFrame& output,
                                        const ChromaKeySettings& settings);
ChromaResult dispatch_chroma_key_gpu(const ChromaDispatchPacket& packet,
                                     const ChromaDispatch& dispatch);
std::uint64_t chroma_frame_digest(const ChromaFrame& frame) noexcept;
std::string_view chroma_shader_source(ChromaBackend backend) noexcept;

}  // namespace digitor

extern "C" {
struct DigitorChromaKeySettings {
  std::uint32_t key_mode;
  float key_r, key_g, key_b;
  float similarity, softness, edge_shrink, edge_blur, despill, spill_balance;
  std::uint32_t invert_matte;
};
std::uint32_t digitor_chroma_key_rgba32f(const float* input_rgba,
                                         float* output_rgba,
                                         std::uint32_t width,
                                         std::uint32_t height,
                                         const DigitorChromaKeySettings* settings,
                                         std::uint64_t* out_digest);
}
