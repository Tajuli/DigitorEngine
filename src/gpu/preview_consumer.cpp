#include "gpu/preview_consumer.hpp"

namespace digitor {
PreviewConsumerDestination::PreviewConsumerDestination(PreviewConsumerMetadata metadata,
    std::uint64_t ownership_token, std::shared_ptr<void> native_destination,
    std::shared_ptr<std::atomic_bool> live, NativeSubmit submit)
    : metadata_(metadata), ownership_token_(ownership_token),
      native_destination_(std::move(native_destination)), live_(std::move(live)),
      submit_(std::move(submit)) {}

DigitorResult PreviewConsumerDestination::submit(const ProcessedGpuFramePtr& frame) noexcept {
  std::scoped_lock lock(mutex_);
  if (!frame || !ownership_token_ || !native_destination_ || !live_ || !submit_ ||
      !live_->load(std::memory_order_acquire)) return DIGITOR_RESULT_NOT_INITIALIZED;
  const auto& source = frame->metadata();
  if (frame->backend() != metadata_.backend || source.width != metadata_.width ||
      source.height != metadata_.height || source.format != metadata_.format ||
      metadata_.precision != GpuPrecisionMode::Float32 || !metadata_.context_identity)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  const auto acquire = frame->acquire(metadata_.context_identity, metadata_.backend);
  if (acquire != DIGITOR_RESULT_OK) return acquire;
  DigitorResult result=DIGITOR_RESULT_INTERNAL_ERROR;
  try { result=submit_(frame,native_destination_); } catch (...) {}
  const auto release = frame->release(metadata_.context_identity);
  if (result == DIGITOR_RESULT_OK && release == DIGITOR_RESULT_OK)
    submissions_.fetch_add(1, std::memory_order_relaxed);
  return result != DIGITOR_RESULT_OK ? result : release;
}

void PreviewConsumerDestination::retire() noexcept {
  std::scoped_lock lock(mutex_);
  if (live_) live_->store(false, std::memory_order_release);
  native_destination_.reset();
}
std::uint64_t PreviewConsumerDestination::submission_count() const noexcept {
  return submissions_.load(std::memory_order_relaxed);
}
} // namespace digitor
