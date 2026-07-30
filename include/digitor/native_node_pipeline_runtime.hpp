#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "digitor/native_node_shader_contracts.hpp"
namespace digitor {
struct NativeNodeDispatchGeometry {
  std::uint32_t groups_x{}, groups_y{}, groups_z{};
};
struct NativeNodeCompiledPipeline {
  DigitorRendererBackend backend{DIGITOR_RENDERER_CPU};
  NativeNodeKernel kernel{NativeNodeKernel::parallel_mixer};
  std::uint64_t contract_hash{};
  NativeNodeDispatchGeometry geometry{};
  bool ready{};
  std::string diagnostic;
};
[[nodiscard]] std::uint64_t native_node_pipeline_contract_hash(
    DigitorRendererBackend, NativeNodeKernel) noexcept;
[[nodiscard]] NativeNodeDispatchGeometry native_node_dispatch_geometry(
    const NativeNodePipelineContract&, std::uint32_t width,
    std::uint32_t height) noexcept;
[[nodiscard]] NativeNodeCompiledPipeline prepare_native_node_pipeline(
    DigitorRendererBackend, NativeNodeKernel, std::uint32_t width,
    std::uint32_t height) noexcept;
}
