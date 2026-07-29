#pragma once

#include "digitor/digitor.h"
#include "gpu/gpu_source.hpp"

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace digitor {
struct NativePipelineCacheKey {
  DigitorRendererBackend backend{DIGITOR_RENDERER_AUTO};
  std::uint64_t device_context_identity{};
  std::string shader_operation_identity;
  std::uint32_t schema_version{1};
  GpuPrecisionMode precision{GpuPrecisionMode::Float32};
  DigitorPixelFormat target_format{DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT};
  auto operator<=>(const NativePipelineCacheKey&) const = default;
};
struct NativePipelineCacheCounters {
  std::uint64_t lookups{}, misses{}, hits{}, creations{}, evictions{},
      invalidations{}, creation_failures{};
};

// Thread-safe bounded FIFO cache. Hits deliberately do not perturb ordering,
// making eviction deterministic across otherwise equivalent executions.
class NativePipelineCache final {
public:
  using Object = std::shared_ptr<void>;
  using Creator = std::function<Object()>;
  explicit NativePipelineCache(std::size_t capacity) : capacity_(capacity) {}
  [[nodiscard]] Object get_or_create(const NativePipelineCacheKey& key,
                                     const Creator& creator) {
    std::scoped_lock lock(mutex_);
    ++counters_.lookups;
    if (const auto found = entries_.find(key); found != entries_.end()) {
      ++counters_.hits;
      return found->second.object;
    }
    ++counters_.misses;
    Object object = creator ? creator() : Object{};
    if (!object) { ++counters_.creation_failures; return {}; }
    ++counters_.creations;
    if (!capacity_) return object;
    if (entries_.size() == capacity_) {
      entries_.erase(order_.front()); order_.pop_front(); ++counters_.evictions;
    }
    order_.push_back(key);
    entries_.emplace(key, Entry{std::move(object)});
    return entries_.find(key)->second.object;
  }
  void invalidate_device(DigitorRendererBackend backend, std::uint64_t identity) {
    std::scoped_lock lock(mutex_);
    for (auto it = order_.begin(); it != order_.end();) {
      if (it->backend == backend && it->device_context_identity == identity) {
        entries_.erase(*it); it = order_.erase(it); ++counters_.invalidations;
      } else ++it;
    }
  }
  [[nodiscard]] NativePipelineCacheCounters counters() const {
    std::scoped_lock lock(mutex_); return counters_;
  }
  [[nodiscard]] std::size_t size() const {
    std::scoped_lock lock(mutex_); return entries_.size();
  }
private:
  struct Entry { Object object; };
  std::size_t capacity_{};
  mutable std::mutex mutex_;
  std::map<NativePipelineCacheKey, Entry> entries_;
  std::list<NativePipelineCacheKey> order_;
  NativePipelineCacheCounters counters_;
};
} // namespace digitor
