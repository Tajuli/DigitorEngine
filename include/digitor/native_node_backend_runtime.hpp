#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "digitor/native_node_pipeline_objects.hpp"

namespace digitor {

enum class NativeNodeBinaryFormat : std::uint32_t { spirv=0, dxil=1, metallib=2, glsl_es=3 };

struct NativeNodeShaderBinary {
  NativeNodeBinaryFormat format{NativeNodeBinaryFormat::spirv};
  std::vector<std::byte> bytes;
  std::uint64_t contract_hash{};
  [[nodiscard]] bool valid_for(const NativeNodeCompiledPipeline&) const noexcept;
};

struct NativeNodeTextureBinding {
  std::uint32_t slot{};
  std::uintptr_t native_texture{};
  std::uint32_t width{};
  std::uint32_t height{};
};

struct NativeNodeDispatchResources {
  std::vector<NativeNodeTextureBinding> textures;
  std::vector<std::byte> constants;
};

struct NativeNodeBackendPipelineHandle {
  std::uintptr_t pipeline{};
  std::uintptr_t layout{};
  std::uintptr_t auxiliary{};
};

using NativeNodeCompileBinaryFn = std::function<bool(
    const NativeNodeCompiledPipeline&, NativeNodeShaderBinary&, std::string&)>;
using NativeNodeCreateBackendPipelineFn = std::function<bool(
    const NativeNodeCompiledPipeline&, const NativeNodeShaderBinary&,
    std::uint64_t device_identity, NativeNodeBackendPipelineHandle&, std::string&)>;
using NativeNodeDestroyBackendPipelineFn =
    std::function<void(const NativeNodeBackendPipelineHandle&)>;
using NativeNodeRecordBackendDispatchFn = std::function<bool(
    const NativeNodeBackendPipelineHandle&, const NativeNodeDispatchGeometry&,
    const NativeNodeDispatchResources&, std::string&)>;

class NativeNodeBackendRuntime {
 public:
  NativeNodeBackendRuntime(NativeNodeCompileBinaryFn,
                           NativeNodeCreateBackendPipelineFn,
                           NativeNodeDestroyBackendPipelineFn,
                           NativeNodeRecordBackendDispatchFn);
  ~NativeNodeBackendRuntime();

  [[nodiscard]] bool prepare(DigitorRendererBackend, NativeNodeKernel,
                             std::uint32_t width, std::uint32_t height,
                             std::uint64_t device_identity,
                             std::string& diagnostic) noexcept;
  [[nodiscard]] bool dispatch(DigitorRendererBackend, NativeNodeKernel,
                              std::uint32_t width, std::uint32_t height,
                              std::uint64_t device_identity,
                              const NativeNodeDispatchResources&,
                              std::string& diagnostic) noexcept;
  void retire_device(std::uint64_t) noexcept;
  void clear() noexcept;
  [[nodiscard]] std::size_t size() const noexcept;

 private:
  struct Entry;
  struct Key;
  struct KeyHash;
  NativeNodeCompileBinaryFn compile_;
  NativeNodeCreateBackendPipelineFn create_;
  NativeNodeDestroyBackendPipelineFn destroy_;
  NativeNodeRecordBackendDispatchFn record_;
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, std::shared_ptr<Entry>> entries_;
  [[nodiscard]] static std::uint64_t key(DigitorRendererBackend, NativeNodeKernel,
                                         std::uint64_t contract_hash,
                                         std::uint64_t device_identity) noexcept;
};

[[nodiscard]] bool validate_native_node_dispatch_resources(
    const NativeNodePipelineContract&, const NativeNodeDispatchResources&,
    std::string& diagnostic) noexcept;

} // namespace digitor
