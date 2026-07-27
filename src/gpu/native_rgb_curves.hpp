#pragma once

#include "digitor/rgb_curves.hpp"

#include <condition_variable>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace digitor {

// Type-erased owner; the deleter is supplied by the backend and therefore runs
// before its device is destroyed. The CPU compiled-LUT cache owns none of this.
struct NativeRgbCurvesResource {
  std::string identity;
  std::shared_ptr<void> native;
  std::size_t uploaded_bytes{};
};

class NativeRgbCurvesCache final {
public:
  struct Lookup { std::shared_ptr<const NativeRgbCurvesResource> resource; bool hit{}; };
  using Factory = std::function<std::shared_ptr<const NativeRgbCurvesResource>()>;
  explicit NativeRgbCurvesCache(std::size_t capacity = 64) : capacity_(capacity) {}
  Lookup get_or_create(const NativeRgbCurvesKey&, const Factory&);
  void clear();
  [[nodiscard]] std::size_t size() const;
private:
  struct Entry { std::shared_ptr<const NativeRgbCurvesResource> value; bool building{}; std::condition_variable ready; std::list<std::string>::iterator lru; };
  std::size_t capacity_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<Entry>> entries_;
  std::list<std::string> lru_;
};

} // namespace digitor
