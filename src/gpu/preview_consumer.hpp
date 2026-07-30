#pragma once

#include "digitor/gpu_frame.hpp"
#include "gpu/gpu_source.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace digitor {

struct PreviewConsumerMetadata {
  DigitorRendererBackend backend{DIGITOR_RENDERER_AUTO};
  const void* context_identity{};
  std::uint32_t width{};
  std::uint32_t height{};
  DigitorPixelFormat format{DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT};
  GpuPrecisionMode precision{GpuPrecisionMode::Float32};
};

// The destination and its liveness token are owned by the registered consumer,
// not by ProcessedGpuFrame. The callback is implemented by a native consumer
// (the qualification harness is one such consumer) and must record/submit a
// GPU-only copy, blit, or draw before returning success.
class PreviewConsumerDestination final
    : public std::enable_shared_from_this<PreviewConsumerDestination> {
public:
  using NativeSubmit = std::function<DigitorResult(const ProcessedGpuFramePtr&,
                                                    const std::shared_ptr<void>&)>;
  PreviewConsumerDestination(PreviewConsumerMetadata metadata,
      std::uint64_t ownership_token, std::shared_ptr<void> native_destination,
      std::shared_ptr<std::atomic_bool> live, NativeSubmit submit);
  [[nodiscard]] DigitorResult submit(const ProcessedGpuFramePtr&) noexcept;
  void retire() noexcept;
  [[nodiscard]] std::uint64_t submission_count() const noexcept;
  [[nodiscard]] std::uint64_t ownership_token() const noexcept { return ownership_token_; }
  [[nodiscard]] const PreviewConsumerMetadata& metadata() const noexcept { return metadata_; }
private:
  PreviewConsumerMetadata metadata_;
  std::uint64_t ownership_token_{};
  std::shared_ptr<void> native_destination_;
  std::shared_ptr<std::atomic_bool> live_;
  NativeSubmit submit_;
  std::atomic_uint64_t submissions_{};
  std::atomic_bool retirement_bound_{};
  std::mutex mutex_;
};

} // namespace digitor
