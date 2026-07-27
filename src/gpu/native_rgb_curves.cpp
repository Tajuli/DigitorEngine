#include "gpu/native_rgb_curves.hpp"

#include <stdexcept>

namespace digitor {

NativeRgbCurvesCache::Lookup NativeRgbCurvesCache::get_or_create(
    const NativeRgbCurvesKey& key, const Factory& factory) {
  const auto stable = key.serialize();
  std::shared_ptr<Entry> entry;
  {
    std::unique_lock lock(mutex_);
    if (auto found = entries_.find(stable); found != entries_.end()) {
      entry = found->second;
      while (entry->building) entry->ready.wait(lock);
      if (entry->value) {
        lru_.splice(lru_.begin(), lru_, entry->lru);
        return {entry->value, true};
      }
      entries_.erase(found);
    }
    lru_.push_front(stable);
    entry = std::make_shared<Entry>();
    entry->building = true;
    entry->lru = lru_.begin();
    entries_.emplace(stable, entry);
  }
  std::shared_ptr<const NativeRgbCurvesResource> value;
  try { value = factory(); } catch (...) {
    std::scoped_lock lock(mutex_);
    lru_.erase(entry->lru); entries_.erase(stable); entry->building = false;
    entry->ready.notify_all(); throw;
  }
  {
    std::scoped_lock lock(mutex_);
    entry->value = value; entry->building = false; entry->ready.notify_all();
    while (entries_.size() > capacity_) {
      auto victim = std::prev(lru_.end());
      if (*victim == stable && entries_.size() == 1) break;
      entries_.erase(*victim); lru_.erase(victim);
    }
  }
  return {std::move(value), false};
}

void NativeRgbCurvesCache::clear() { std::scoped_lock lock(mutex_); entries_.clear(); lru_.clear(); }
std::size_t NativeRgbCurvesCache::size() const { std::scoped_lock lock(mutex_); return entries_.size(); }

} // namespace digitor
