#include "digitor/android_zero_copy_pipeline.hpp"

#include <chrono>
#include <mutex>
#include <new>
#include <utility>

#ifdef __ANDROID__
#include <android/hardware_buffer.h>
#include <unistd.h>
#endif

namespace digitor {

struct AndroidZeroCopyPipeline::Impl {
  AndroidZeroCopyConfig config;
  AndroidZeroCopyBinding binding;
  mutable std::mutex mutex;
  AndroidZeroCopyTelemetry telemetry;
  bool initialized{};

  DigitorResult fail(const char* message, bool quarantine_session = false) noexcept {
    std::scoped_lock lock(mutex);
    telemetry.diagnostic = message ? message : "Android zero-copy failure";
    if (quarantine_session) telemetry.quarantined = true;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  DigitorResult validate_frame(const AndroidHardwareBufferFrame& f,
                               std::int64_t requested) noexcept {
    if (!f.hardware_buffer || !f.width || !f.height || !f.decoder_lifetime)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (f.timestamp_us != requested) {
      std::scoped_lock lock(mutex);
      ++telemetry.timestamp_mismatches;
      telemetry.diagnostic = "MediaCodec surface timestamp mismatch";
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
    if (f.bit_depth != 8 && f.bit_depth != 10)
      return DIGITOR_RESULT_UNSUPPORTED;
    if (f.bit_depth == 10 && f.format != AndroidYuvFormat::p010)
      return DIGITOR_RESULT_UNSUPPORTED;
    return DIGITOR_RESULT_OK;
  }

  DigitorResult import(const AndroidHardwareBufferFrame& source,
                       AndroidImportedImage& imported) noexcept {
    auto attempt = [&](AndroidZeroCopyBackend backend) noexcept -> DigitorResult {
      imported = {};
      DigitorResult r = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      if (backend == AndroidZeroCopyBackend::vulkan && binding.import_vulkan)
        r = binding.import_vulkan(source, imported);
      else if (backend == AndroidZeroCopyBackend::opengl_es && binding.import_gles)
        r = binding.import_gles(source, imported);
      if (r != DIGITOR_RESULT_OK) return r;
      if (imported.backend != backend || !imported.image || !imported.lifetime ||
          imported.width != source.width || imported.height != source.height ||
          imported.timestamp_us != source.timestamp_us ||
          imported.frame_identity != source.frame_identity)
        return DIGITOR_RESULT_INTERNAL_ERROR;
      if (config.require_external_fence && !imported.completion_sync)
        return DIGITOR_RESULT_UNSUPPORTED;
      return DIGITOR_RESULT_OK;
    };

    DigitorResult result = attempt(config.preferred_backend);
    if (result == DIGITOR_RESULT_OK) return result;
    {
      std::scoped_lock lock(mutex);
      ++telemetry.import_failures;
    }
    if (config.preferred_backend == AndroidZeroCopyBackend::vulkan &&
        config.allow_gles_fallback)
      result = attempt(AndroidZeroCopyBackend::opengl_es);
    return result;
  }

  DigitorResult produce(std::int64_t timestamp_us,
                        ProcessedGpuFramePtr& output) noexcept {
    output.reset();
    if (!initialized || telemetry.quarantined)
      return DIGITOR_RESULT_NOT_INITIALIZED;

    const auto started = std::chrono::steady_clock::now();
    AndroidHardwareBufferFrame decoded;
    {
      std::scoped_lock lock(mutex);
      ++telemetry.frames_requested;
    }
    auto result = binding.acquire_decoder_frame(timestamp_us, decoded);
    if (result != DIGITOR_RESULT_OK)
      return fail("MediaCodec hardware surface acquisition failed", true);
    result = validate_frame(decoded, timestamp_us);
    if (result != DIGITOR_RESULT_OK) return result;
    {
      std::scoped_lock lock(mutex);
      ++telemetry.decoder_surfaces;
    }

    AndroidImportedImage imported;
    result = import(decoded, imported);
    if (result != DIGITOR_RESULT_OK)
      return fail("AHardwareBuffer external-image import failed", true);
    {
      std::scoped_lock lock(mutex);
      telemetry.active_backend = imported.backend;
      ++telemetry.imported_images;
    }

    result = binding.convert_to_rgba16f(imported, output);
    if (result != DIGITOR_RESULT_OK || !output)
      return fail("GPU YUV to RGBA16F conversion failed", true);
    if (!output->ready() ||
        output->metadata().format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT ||
        output->metadata().timestamp != timestamp_us ||
        output->identity() != imported.frame_identity)
      return fail("RGBA16F output identity contract failed", true);

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    if (elapsed > config.frame_timeout_ms)
      return fail("Android zero-copy frame exceeded deadline", true);

    {
      std::scoped_lock lock(mutex);
      ++telemetry.rgba16f_frames;
      telemetry.diagnostic = "Android zero-copy RGBA16F frame ready";
    }
    return DIGITOR_RESULT_OK;
  }
};

AndroidZeroCopyPipeline::AndroidZeroCopyPipeline(AndroidZeroCopyConfig c,
                                                 AndroidZeroCopyBinding b)
    : impl_(std::make_unique<Impl>()) {
  impl_->config = std::move(c);
  impl_->binding = std::move(b);
}
AndroidZeroCopyPipeline::~AndroidZeroCopyPipeline() = default;

DigitorResult AndroidZeroCopyPipeline::initialize() noexcept {
  try {
    auto& i = *impl_;
    if (!i.config.strict_gpu_first || !i.config.require_external_memory ||
        !i.config.require_rgba16f_output || !i.config.max_in_flight_frames ||
        !i.binding.acquire_decoder_frame || !i.binding.convert_to_rgba16f)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (i.config.preferred_backend == AndroidZeroCopyBackend::vulkan &&
        !i.binding.import_vulkan &&
        !(i.config.allow_gles_fallback && i.binding.import_gles))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    if (i.config.preferred_backend == AndroidZeroCopyBackend::opengl_es &&
        !i.binding.import_gles)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    if (!i.binding.preview_consumer || !i.binding.encoder_consumer)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    std::scoped_lock lock(i.mutex);
    i.initialized = true;
    i.telemetry.quarantined = false;
    i.telemetry.cpu_copies = 0;
    i.telemetry.cpu_fallback_frames = 0;
    i.telemetry.diagnostic = "Android zero-copy pipeline initialized";
    return DIGITOR_RESULT_OK;
  } catch (const std::bad_alloc&) {
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

DigitorResult AndroidZeroCopyPipeline::preview(std::int64_t timestamp_us) noexcept {
  ProcessedGpuFramePtr frame;
  auto result = impl_->produce(timestamp_us, frame);
  if (result != DIGITOR_RESULT_OK) return result;
  result = impl_->binding.preview_consumer(frame);
  if (result == DIGITOR_RESULT_OK) {
    std::scoped_lock lock(impl_->mutex);
    ++impl_->telemetry.preview_frames;
  }
  return result;
}

DigitorResult AndroidZeroCopyPipeline::export_frame(std::int64_t timestamp_us) noexcept {
  ProcessedGpuFramePtr frame;
  auto result = impl_->produce(timestamp_us, frame);
  if (result != DIGITOR_RESULT_OK) return result;
  result = impl_->binding.encoder_consumer(frame);
  if (result == DIGITOR_RESULT_OK) {
    std::scoped_lock lock(impl_->mutex);
    ++impl_->telemetry.encoder_frames;
  }
  return result;
}

DigitorResult AndroidZeroCopyPipeline::preview_and_export(
    std::int64_t timestamp_us) noexcept {
  ProcessedGpuFramePtr frame;
  auto result = impl_->produce(timestamp_us, frame);
  if (result != DIGITOR_RESULT_OK) return result;
  const auto identity = frame->identity();
  result = impl_->binding.preview_consumer(frame);
  if (result != DIGITOR_RESULT_OK) return result;
  result = impl_->binding.encoder_consumer(frame);
  if (result != DIGITOR_RESULT_OK) return result;
  if (impl_->config.require_preview_export_identity && frame->identity() != identity) {
    std::scoped_lock lock(impl_->mutex);
    ++impl_->telemetry.identity_mismatches;
    impl_->telemetry.quarantined = true;
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
  {
    std::scoped_lock lock(impl_->mutex);
    ++impl_->telemetry.preview_frames;
    ++impl_->telemetry.encoder_frames;
  }
  return DIGITOR_RESULT_OK;
}

DigitorResult AndroidZeroCopyPipeline::quarantine(const char* reason) noexcept {
  std::scoped_lock lock(impl_->mutex);
  impl_->telemetry.quarantined = true;
  impl_->telemetry.diagnostic = reason ? reason : "Android pipeline quarantined";
  return DIGITOR_RESULT_OK;
}

DigitorResult AndroidZeroCopyPipeline::reset_quarantine() noexcept {
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->initialized) return DIGITOR_RESULT_NOT_INITIALIZED;
  impl_->telemetry.quarantined = false;
  impl_->telemetry.diagnostic = "Android pipeline quarantine reset";
  return DIGITOR_RESULT_OK;
}

AndroidZeroCopyTelemetry AndroidZeroCopyPipeline::telemetry() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->telemetry;
}

bool AndroidZeroCopyPipeline::production_active() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return impl_->initialized && !impl_->telemetry.quarantined &&
         impl_->telemetry.cpu_copies == 0 &&
         impl_->telemetry.cpu_fallback_frames == 0;
}

} // namespace digitor
