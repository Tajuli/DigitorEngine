#pragma once
#include "digitor/color.hpp"
#include "digitor/production_node_graph.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace digitor {
// Authoritative FP32 reference contracts for backend-native node kernels.
// Native Vulkan/D3D12/Metal/GLES implementations must match these semantics.
void node_mixer_reference(std::span<const std::span<const Color>> inputs,
                          std::span<const float> weights,
                          std::span<Color> output);
void power_window_matte_reference(const PowerWindowSettings& settings,
                                  std::uint32_t width, std::uint32_t height,
                                  std::span<float> matte);
void masked_composite_reference(std::span<const Color> original,
                                std::span<const Color> processed,
                                std::span<const float> matte,
                                std::span<Color> output);
}
