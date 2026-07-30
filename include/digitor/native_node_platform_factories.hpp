#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cstddef>
#include "digitor/native_node_backend_runtime.hpp"

namespace digitor {

struct NativeNodePlatformFactoryContext {
  std::uintptr_t device{};
  std::uintptr_t command_context{};
  // Vulkan: VkDescriptorPool. D3D12: ID3D12DescriptorHeap*.
  std::uintptr_t descriptor_context{};
  // D3D12 only: descriptor increment size.
  std::uint32_t descriptor_stride{};
};

[[nodiscard]] bool create_vulkan_native_node_pipeline(
    const NativeNodePlatformFactoryContext&, const NativeNodeCompiledPipeline&,
    const NativeNodeShaderBinary&, NativeNodeBackendPipelineHandle&,
    std::string&) noexcept;
void destroy_vulkan_native_node_pipeline(
    const NativeNodePlatformFactoryContext&,
    const NativeNodeBackendPipelineHandle&) noexcept;
[[nodiscard]] bool record_vulkan_native_node_dispatch(
    const NativeNodePlatformFactoryContext&,
    const NativeNodeBackendPipelineHandle&, const NativeNodeDispatchGeometry&,
    const NativeNodeDispatchResources&, std::string&) noexcept;

[[nodiscard]] bool create_d3d12_native_node_pipeline(
    const NativeNodePlatformFactoryContext&, const NativeNodeCompiledPipeline&,
    const NativeNodeShaderBinary&, NativeNodeBackendPipelineHandle&,
    std::string&) noexcept;
void destroy_d3d12_native_node_pipeline(
    const NativeNodePlatformFactoryContext&,
    const NativeNodeBackendPipelineHandle&) noexcept;
[[nodiscard]] bool record_d3d12_native_node_dispatch(
    const NativeNodePlatformFactoryContext&,
    const NativeNodeBackendPipelineHandle&, const NativeNodeDispatchGeometry&,
    const NativeNodeDispatchResources&, std::string&) noexcept;

} // namespace digitor

namespace digitor {

enum class NativeNodeAccessState : std::uint32_t {
  unknown = 0,
  shader_read = 1,
  shader_write = 2,
  encoder_read = 3
};

struct NativeNodeTextureTransition {
  std::uintptr_t native_resource{};
  NativeNodeAccessState before{NativeNodeAccessState::unknown};
  NativeNodeAccessState after{NativeNodeAccessState::unknown};
};

struct NativeNodeRetiredDescriptor {
  std::uintptr_t native_handle{};
  std::uint64_t completion_value{};
};

class NativeNodeDescriptorRetirementQueue {
 public:
  void retain(std::uintptr_t native_handle, std::uint64_t completion_value);
  [[nodiscard]] std::vector<std::uintptr_t> collect(std::uint64_t completed_value);
  [[nodiscard]] std::size_t size() const noexcept { return pending_.size(); }
  void clear() noexcept { pending_.clear(); }
 private:
  std::vector<NativeNodeRetiredDescriptor> pending_;
};

[[nodiscard]] bool record_vulkan_native_node_barriers(
    const NativeNodePlatformFactoryContext&,
    const std::vector<NativeNodeTextureTransition>&, std::string&) noexcept;
[[nodiscard]] bool record_d3d12_native_node_barriers(
    const NativeNodePlatformFactoryContext&,
    const std::vector<NativeNodeTextureTransition>&, std::string&) noexcept;

} // namespace digitor

namespace digitor {
[[nodiscard]] bool create_metal_native_node_pipeline(
    const NativeNodePlatformFactoryContext&, const NativeNodeCompiledPipeline&,
    const NativeNodeShaderBinary&, NativeNodeBackendPipelineHandle&,
    std::string&) noexcept;
void destroy_metal_native_node_pipeline(
    const NativeNodePlatformFactoryContext&,
    const NativeNodeBackendPipelineHandle&) noexcept;
[[nodiscard]] bool record_metal_native_node_dispatch(
    const NativeNodePlatformFactoryContext&,
    const NativeNodeBackendPipelineHandle&, const NativeNodeDispatchGeometry&,
    const NativeNodeDispatchResources&, std::string&) noexcept;

[[nodiscard]] bool create_gles_native_node_pipeline(
    const NativeNodePlatformFactoryContext&, const NativeNodeCompiledPipeline&,
    const NativeNodeShaderBinary&, NativeNodeBackendPipelineHandle&,
    std::string&) noexcept;
void destroy_gles_native_node_pipeline(
    const NativeNodePlatformFactoryContext&,
    const NativeNodeBackendPipelineHandle&) noexcept;
[[nodiscard]] bool record_gles_native_node_dispatch(
    const NativeNodePlatformFactoryContext&,
    const NativeNodeBackendPipelineHandle&, const NativeNodeDispatchGeometry&,
    const NativeNodeDispatchResources&, std::string&) noexcept;
} // namespace digitor
