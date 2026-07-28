#include "digitor/gpu_frame.hpp"

namespace digitor {

ProcessedGpuFrame::ProcessedGpuFrame(const void* context,
    DigitorRendererBackend backend, GpuFrameMetadata metadata,
    std::uint64_t identity, NativeOwner native,
    std::shared_ptr<std::atomic_bool> ready, bool validation_readback_supported)
    : context_(context), backend_(backend), metadata_(std::move(metadata)),
      identity_(identity), native_(std::move(native)), ready_(std::move(ready)),
      validation_readback_supported_(validation_readback_supported) {}

DigitorResult ProcessedGpuFrame::acquire(const void* context,
                                         DigitorRendererBackend consumer) noexcept {
  if (!context || context != context_ || consumer != backend_)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  if (!native_ || !ready()) return DIGITOR_RESULT_RESOURCE_IN_USE;
  acquisitions_.fetch_add(1, std::memory_order_acq_rel);
  return DIGITOR_RESULT_OK;
}

DigitorResult ProcessedGpuFrame::release(const void* context) noexcept {
  if (!context || context != context_) return DIGITOR_RESULT_INVALID_ARGUMENT;
  auto count = acquisitions_.load(std::memory_order_acquire);
  while (count != 0) {
    if (acquisitions_.compare_exchange_weak(count, count - 1,
                                            std::memory_order_acq_rel))
      return DIGITOR_RESULT_OK;
  }
  return DIGITOR_RESULT_INVALID_ARGUMENT;
}

bool ProcessedGpuFrame::ready() const noexcept {
  return ready_ && ready_->load(std::memory_order_acquire);
}

} // namespace digitor
