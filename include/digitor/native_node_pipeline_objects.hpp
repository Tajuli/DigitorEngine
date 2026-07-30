#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include "digitor/native_node_pipeline_runtime.hpp"

namespace digitor {

enum class NativeNodePipelineObjectState : std::uint32_t {
  empty = 0,
  ready,
  failed,
  retired,
};

struct NativeNodePipelineObject {
  DigitorRendererBackend backend{DIGITOR_RENDERER_CPU};
  NativeNodeKernel kernel{NativeNodeKernel::parallel_mixer};
  std::uint64_t contract_hash{};
  std::uint64_t device_identity{};
  std::uintptr_t native_handle{};
  NativeNodePipelineObjectState state{NativeNodePipelineObjectState::empty};
  std::string diagnostic;
};

struct NativeNodePipelineObjectCounters {
  std::uint64_t creates{};
  std::uint64_t cache_hits{};
  std::uint64_t dispatches{};
  std::uint64_t failures{};
  std::uint64_t retires{};
};

using NativeNodePipelineCreateFn = std::function<bool(
    const NativeNodeCompiledPipeline&, std::uint64_t device_identity,
    std::uintptr_t& native_handle, std::string& diagnostic)>;
using NativeNodePipelineDestroyFn = std::function<void(std::uintptr_t)>;
using NativeNodePipelineDispatchFn = std::function<bool(
    std::uintptr_t native_handle, const NativeNodeDispatchGeometry&,
    std::string& diagnostic)>;

class NativeNodePipelineObjectCache {
 public:
  NativeNodePipelineObjectCache(NativeNodePipelineCreateFn,
                                NativeNodePipelineDestroyFn,
                                NativeNodePipelineDispatchFn);
  ~NativeNodePipelineObjectCache();

  NativeNodePipelineObjectCache(const NativeNodePipelineObjectCache&) = delete;
  NativeNodePipelineObjectCache& operator=(const NativeNodePipelineObjectCache&) = delete;

  [[nodiscard]] std::shared_ptr<const NativeNodePipelineObject> acquire(
      DigitorRendererBackend, NativeNodeKernel, std::uint32_t width,
      std::uint32_t height, std::uint64_t device_identity) noexcept;

  [[nodiscard]] bool dispatch(
      const std::shared_ptr<const NativeNodePipelineObject>&,
      const NativeNodeDispatchGeometry&, std::string& diagnostic) noexcept;

  void retire_device(std::uint64_t device_identity) noexcept;
  void clear() noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] NativeNodePipelineObjectCounters counters() const noexcept;

 private:
  struct Key {
    DigitorRendererBackend backend{DIGITOR_RENDERER_CPU};
    NativeNodeKernel kernel{NativeNodeKernel::parallel_mixer};
    std::uint64_t contract_hash{};
    std::uint64_t device_identity{};
    bool operator==(const Key&) const noexcept = default;
  };
  struct KeyHash {
    std::size_t operator()(const Key&) const noexcept;
  };

  NativeNodePipelineCreateFn create_;
  NativeNodePipelineDestroyFn destroy_;
  NativeNodePipelineDispatchFn dispatch_;
  mutable std::mutex mutex_;
  std::unordered_map<Key, std::shared_ptr<NativeNodePipelineObject>, KeyHash> objects_;
  NativeNodePipelineObjectCounters counters_{};
};

}  // namespace digitor
