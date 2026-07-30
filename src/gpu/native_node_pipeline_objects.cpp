#include "digitor/native_node_pipeline_objects.hpp"
#include <utility>

namespace digitor {

std::size_t NativeNodePipelineObjectCache::KeyHash::operator()(const Key& k) const noexcept {
  std::size_t h = static_cast<std::size_t>(k.backend);
  h ^= static_cast<std::size_t>(k.kernel) + 0x9e3779b9u + (h << 6) + (h >> 2);
  h ^= static_cast<std::size_t>(k.contract_hash) + 0x9e3779b9u + (h << 6) + (h >> 2);
  h ^= static_cast<std::size_t>(k.device_identity) + 0x9e3779b9u + (h << 6) + (h >> 2);
  return h;
}

NativeNodePipelineObjectCache::NativeNodePipelineObjectCache(
    NativeNodePipelineCreateFn create, NativeNodePipelineDestroyFn destroy,
    NativeNodePipelineDispatchFn dispatch)
    : create_(std::move(create)), destroy_(std::move(destroy)),
      dispatch_(std::move(dispatch)) {}

NativeNodePipelineObjectCache::~NativeNodePipelineObjectCache() { clear(); }

std::shared_ptr<const NativeNodePipelineObject>
NativeNodePipelineObjectCache::acquire(DigitorRendererBackend backend,
                                       NativeNodeKernel kernel,
                                       std::uint32_t width,
                                       std::uint32_t height,
                                       std::uint64_t device_identity) noexcept {
  try {
    auto prepared = prepare_native_node_pipeline(backend, kernel, width, height);
    if (!prepared.ready || prepared.contract_hash == 0 || device_identity == 0 || !create_) {
      std::lock_guard lock(mutex_);
      ++counters_.failures;
      return {};
    }
    const Key key{backend, kernel, prepared.contract_hash, device_identity};
    {
      std::lock_guard lock(mutex_);
      auto it = objects_.find(key);
      if (it != objects_.end() && it->second->state == NativeNodePipelineObjectState::ready) {
        ++counters_.cache_hits;
        return it->second;
      }
    }

    std::uintptr_t handle{};
    std::string diagnostic;
    if (!create_(prepared, device_identity, handle, diagnostic) || handle == 0) {
      std::lock_guard lock(mutex_);
      ++counters_.failures;
      return {};
    }

    auto object = std::make_shared<NativeNodePipelineObject>();
    object->backend = backend;
    object->kernel = kernel;
    object->contract_hash = prepared.contract_hash;
    object->device_identity = device_identity;
    object->native_handle = handle;
    object->state = NativeNodePipelineObjectState::ready;
    object->diagnostic = std::move(diagnostic);

    std::lock_guard lock(mutex_);
    auto [it, inserted] = objects_.emplace(key, object);
    if (!inserted) {
      if (destroy_) destroy_(handle);
      ++counters_.cache_hits;
      return it->second;
    }
    ++counters_.creates;
    return object;
  } catch (...) {
    std::lock_guard lock(mutex_);
    ++counters_.failures;
    return {};
  }
}

bool NativeNodePipelineObjectCache::dispatch(
    const std::shared_ptr<const NativeNodePipelineObject>& object,
    const NativeNodeDispatchGeometry& geometry,
    std::string& diagnostic) noexcept {
  if (!object || object->state != NativeNodePipelineObjectState::ready ||
      object->native_handle == 0 || geometry.groups_x == 0 ||
      geometry.groups_y == 0 || geometry.groups_z == 0 || !dispatch_) {
    std::lock_guard lock(mutex_);
    ++counters_.failures;
    diagnostic = "invalid native node pipeline dispatch";
    return false;
  }
  try {
    if (!dispatch_(object->native_handle, geometry, diagnostic)) {
      std::lock_guard lock(mutex_);
      ++counters_.failures;
      return false;
    }
    std::lock_guard lock(mutex_);
    ++counters_.dispatches;
    return true;
  } catch (...) {
    std::lock_guard lock(mutex_);
    ++counters_.failures;
    diagnostic = "native node pipeline dispatch threw";
    return false;
  }
}

void NativeNodePipelineObjectCache::retire_device(std::uint64_t device_identity) noexcept {
  std::lock_guard lock(mutex_);
  for (auto it = objects_.begin(); it != objects_.end();) {
    if (it->first.device_identity != device_identity) { ++it; continue; }
    if (destroy_ && it->second->native_handle) destroy_(it->second->native_handle);
    it->second->state = NativeNodePipelineObjectState::retired;
    it->second->native_handle = 0;
    ++counters_.retires;
    it = objects_.erase(it);
  }
}

void NativeNodePipelineObjectCache::clear() noexcept {
  std::lock_guard lock(mutex_);
  for (auto& [_, object] : objects_) {
    if (destroy_ && object->native_handle) destroy_(object->native_handle);
    object->state = NativeNodePipelineObjectState::retired;
    object->native_handle = 0;
    ++counters_.retires;
  }
  objects_.clear();
}

std::size_t NativeNodePipelineObjectCache::size() const noexcept {
  std::lock_guard lock(mutex_);
  return objects_.size();
}

NativeNodePipelineObjectCounters NativeNodePipelineObjectCache::counters() const noexcept {
  std::lock_guard lock(mutex_);
  return counters_;
}

}  // namespace digitor
