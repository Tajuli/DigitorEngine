#include "digitor/gpu_frame.hpp"

namespace digitor {

void GpuContextLifetime::add_retirement_callback(RetirementCallback callback) {
  if (!callback) return;
  if (!live()) { callback(); return; }
  std::scoped_lock lock(retirement_mutex_);
  if (!live()) { callback(); return; }
  retirement_callbacks_.push_back(std::move(callback));
}

void GpuContextLifetime::retire() noexcept {
  if (!live_.exchange(false, std::memory_order_acq_rel)) return;
  std::vector<RetirementCallback> callbacks;
  {
    std::scoped_lock lock(retirement_mutex_);
    callbacks.swap(retirement_callbacks_);
  }
  for (auto &callback : callbacks) {
    try { callback(); } catch (...) {}
  }
}

ProcessedGpuFrame::ProcessedGpuFrame(const void* context,
    DigitorRendererBackend backend, GpuFrameMetadata metadata,
    std::uint64_t identity, NativeOwner native,
    std::shared_ptr<std::atomic_bool> ready, bool validation_readback_supported,
    ValidationReadback validation_readback)
    : context_(context), backend_(backend), metadata_(std::move(metadata)),
      identity_(identity), native_holder_(std::make_shared<NativeOwner>(std::move(native))),
      ready_(std::move(ready)), validation_readback_supported_(validation_readback_supported),
      validation_readback_(std::move(validation_readback)) {}

DigitorResult ProcessedGpuFrame::acquire(const void* context,
                                         DigitorRendererBackend consumer) noexcept {
  if (!context || context != context_ || consumer != backend_)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  if (!context_live()) return DIGITOR_RESULT_NOT_INITIALIZED;
  if (!native_holder_ || !*native_holder_ || !ready()) return DIGITOR_RESULT_RESOURCE_IN_USE;
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
  return context_live() && ready_ && ready_->load(std::memory_order_acquire);
}

DigitorResult ProcessedGpuFrame::validation_readback(std::vector<float>& out) const noexcept {
  out.clear();
  if (!validation_readback_supported() || !context_live())
    return DIGITOR_RESULT_UNSUPPORTED;
  if (!ready()) return DIGITOR_RESULT_RESOURCE_IN_USE;
  try { return validation_readback_(out); }
  catch (const std::bad_alloc&) { out.clear(); return DIGITOR_RESULT_OUT_OF_MEMORY; }
  catch (...) { out.clear(); return DIGITOR_RESULT_INTERNAL_ERROR; }
}

bool ProcessedGpuFrame::context_live() const noexcept {
  const auto lifetime = context_lifetime_.lock();
  return !context_lifetime_bound_ || (lifetime && lifetime->live());
}

void ProcessedGpuFrame::add_context_retirement_callback(
    GpuContextLifetime::RetirementCallback callback) const noexcept {
  if (!callback) return;
  try {
    if (const auto lifetime = context_lifetime_.lock()) {
      lifetime->add_retirement_callback(std::move(callback));
    } else if (context_lifetime_bound_) {
      callback();
    }
  } catch (...) {
    try { callback(); } catch (...) {}
  }
}

void ProcessedGpuFrame::bind_context_lifetime(
    const std::shared_ptr<GpuContextLifetime>& lifetime) noexcept {
  context_lifetime_ = lifetime;
  context_lifetime_bound_ = true;
  if (lifetime && native_holder_) {
    std::weak_ptr<NativeOwner> weak_owner = native_holder_;
    lifetime->add_retirement_callback([weak_owner]() noexcept {
      if (auto owner = weak_owner.lock()) owner->reset();
    });
  }
}

} // namespace digitor
