#pragma once

#include "digitor/digitor.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <string>

namespace digitor {
class IRenderBackend;

// Shared, address-independent context state. Frames keep only a weak reference:
// this makes context retirement observable without dereferencing a backend that
// has already been destroyed.
class GpuContextLifetime final {
public:
  using RetirementCallback = std::function<void()>;
  void retire() noexcept;
  void add_retirement_callback(RetirementCallback callback);
  [[nodiscard]] bool live() const noexcept {
    return live_.load(std::memory_order_acquire);
  }
private:
  std::atomic_bool live_{true};
  mutable std::mutex retirement_mutex_;
  std::vector<RetirementCallback> retirement_callbacks_;
};

enum class GpuFrameAlpha : std::uint32_t { straight = 1, premultiplied = 2 };
enum class PreviewSource : std::uint32_t { none, gpu, cpu_validation };

struct GpuFrameMetadata {
  std::uint32_t width{};
  std::uint32_t height{};
  DigitorPixelFormat format{DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT};
  GpuFrameAlpha alpha{GpuFrameAlpha::straight};
  std::int64_t timestamp{};
  std::string color_metadata{"linear-rgba"};
};

// Backend handles never leave this translation-unit boundary. The native owner
// callback captures the typed RAII object; consumers receive only this object.
class ProcessedGpuFrame final {
public:
  using NativeOwner = std::shared_ptr<void>;
  ProcessedGpuFrame(const void* context, DigitorRendererBackend backend,
                    GpuFrameMetadata metadata, std::uint64_t identity,
                    NativeOwner native, std::shared_ptr<std::atomic_bool> ready,
                    bool validation_readback_supported);

  [[nodiscard]] DigitorResult acquire(const void* context,
                                      DigitorRendererBackend consumer) noexcept;
  [[nodiscard]] DigitorResult release(const void* context) noexcept;
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] const GpuFrameMetadata& metadata() const noexcept { return metadata_; }
  [[nodiscard]] DigitorRendererBackend backend() const noexcept { return backend_; }
  [[nodiscard]] std::uint64_t identity() const noexcept { return identity_; }
  [[nodiscard]] bool validation_readback_supported() const noexcept { return validation_readback_supported_; }
  [[nodiscard]] bool context_live() const noexcept;

private:
  friend class IRenderBackend;
  void bind_context_lifetime(const std::shared_ptr<GpuContextLifetime>&) noexcept;
  const void* context_{};
  DigitorRendererBackend backend_{};
  GpuFrameMetadata metadata_;
  std::uint64_t identity_{};
  std::shared_ptr<NativeOwner> native_holder_;
  std::shared_ptr<std::atomic_bool> ready_;
  std::weak_ptr<GpuContextLifetime> context_lifetime_;
  bool context_lifetime_bound_{};
  bool validation_readback_supported_{};
  std::atomic_uint32_t acquisitions_{0};
};

using ProcessedGpuFramePtr = std::shared_ptr<ProcessedGpuFrame>;

} // namespace digitor
