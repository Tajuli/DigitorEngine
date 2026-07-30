#pragma once

#include "digitor/digitor.h"
#include "digitor/gpu_frame.hpp"

#include <atomic>
#include <cstdint>
#include <memory>

namespace digitor {

enum class GpuMatteFormat : std::uint32_t { r32_float = 1 };

struct GpuMatteMetadata {
  std::uint32_t width{};
  std::uint32_t height{};
  std::int64_t timestamp{};
  GpuMatteFormat format{GpuMatteFormat::r32_float};
};

// Type-distinct backend-owned single-channel matte. A matte can never be
// consumed as an RGBA ProcessedGpuFrame by accident.
class GpuMatteResource final {
public:
  using NativeOwner = std::shared_ptr<void>;

  GpuMatteResource(DigitorRendererBackend backend,
                   std::uint64_t context_identity,
                   GpuMatteMetadata metadata,
                   std::uint64_t identity,
                   NativeOwner native,
                   std::shared_ptr<std::atomic_bool> ready,
                   std::weak_ptr<GpuContextLifetime> context_lifetime) noexcept
      : backend_(backend),
        context_identity_(context_identity),
        metadata_(metadata),
        identity_(identity),
        native_(std::move(native)),
        ready_(std::move(ready)),
        context_lifetime_(std::move(context_lifetime)) {}

  [[nodiscard]] DigitorRendererBackend backend() const noexcept { return backend_; }
  [[nodiscard]] std::uint64_t context_identity() const noexcept { return context_identity_; }
  [[nodiscard]] const GpuMatteMetadata& metadata() const noexcept { return metadata_; }
  [[nodiscard]] std::uint64_t identity() const noexcept { return identity_; }
  [[nodiscard]] bool ready() const noexcept {
    return ready_ && ready_->load(std::memory_order_acquire);
  }
  [[nodiscard]] bool context_live() const noexcept {
    const auto lifetime = context_lifetime_.lock();
    return lifetime && lifetime->live();
  }
  [[nodiscard]] bool usable_by(DigitorRendererBackend backend,
                               std::uint64_t context_identity) const noexcept {
    return backend_ == backend && context_identity_ == context_identity &&
           metadata_.width != 0 && metadata_.height != 0 && ready() &&
           context_live() && native_;
  }
  [[nodiscard]] const NativeOwner& native_owner() const noexcept { return native_; }

private:
  DigitorRendererBackend backend_{DIGITOR_RENDERER_AUTO};
  std::uint64_t context_identity_{};
  GpuMatteMetadata metadata_{};
  std::uint64_t identity_{};
  NativeOwner native_{};
  std::shared_ptr<std::atomic_bool> ready_{};
  std::weak_ptr<GpuContextLifetime> context_lifetime_{};
};

using GpuMatteResourcePtr = std::shared_ptr<GpuMatteResource>;

} // namespace digitor
