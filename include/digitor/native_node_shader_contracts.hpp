#pragma once
#include <cstdint>
#include <string_view>
#include "digitor/digitor.h"
namespace digitor {
enum class NativeNodeKernel : std::uint32_t { parallel_mixer=0, masked_composite=1 };
enum class NativeNodeShaderLanguage : std::uint32_t { unknown=0, spirv_glsl=1, hlsl=2, metal=3, gles_glsl=4 };
enum class NativeNodeBindingKind : std::uint32_t { sampled_or_storage_input=0, storage_output=1, constants=2 };
struct NativeNodeBinding { std::uint32_t binding{}; NativeNodeBindingKind kind{}; std::string_view format; };
struct NativeNodePipelineContract {
  NativeNodeShaderLanguage language{NativeNodeShaderLanguage::unknown};
  std::string_view entry_point;
  std::string_view source;
  const NativeNodeBinding* bindings{};
  std::uint32_t binding_count{};
  std::uint32_t constant_bytes{};
  std::uint32_t local_size_x{8}, local_size_y{8}, local_size_z{1};
  bool uses_push_constants{};
};
using NativeNodeShaderContract = NativeNodePipelineContract;
[[nodiscard]] NativeNodePipelineContract native_node_pipeline_contract(DigitorRendererBackend backend, NativeNodeKernel kernel) noexcept;
[[nodiscard]] NativeNodeShaderContract native_node_shader_contract(DigitorRendererBackend backend, NativeNodeKernel kernel) noexcept;
[[nodiscard]] bool validate_native_node_pipeline_contract(const NativeNodePipelineContract&) noexcept;
}
