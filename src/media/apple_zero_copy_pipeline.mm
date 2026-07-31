#include "digitor/apple_zero_copy_pipeline.hpp"

#include <chrono>
#include <mutex>
#include <new>
#include <utility>

#if defined(__APPLE__)
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#endif

namespace digitor {

struct AppleZeroCopyPipeline::Impl {
  AppleZeroCopyConfig config;
  AppleZeroCopyBinding binding;
  mutable std::mutex mutex;
  AppleZeroCopyTelemetry telemetry;
  bool initialized{};
};

AppleZeroCopyPipeline::AppleZeroCopyPipeline(AppleZeroCopyConfig c, AppleZeroCopyBinding b)
    : impl_(std::make_unique<Impl>()) {
  impl_->config = std::move(c);
  impl_->binding = std::move(b);
}
AppleZeroCopyPipeline::~AppleZeroCopyPipeline() = default;

DigitorResult AppleZeroCopyPipeline::initialize() noexcept {
  try {
    auto& i = *impl_;
    if (!i.config.strict_gpu_first || !i.binding.acquire_decoder_frame ||
        !i.binding.import_metal || !i.binding.convert_to_rgba16f ||
        !i.binding.preview_consumer || !i.binding.encoder_consumer)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    std::scoped_lock lock(i.mutex);
    i.initialized = true;
    i.telemetry.diagnostic = "Apple zero-copy pipeline initialized";
    return DIGITOR_RESULT_OK;
  } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

template <typename ImplT>
static DigitorResult process_frame(ImplT& i, std::int64_t timestamp_us,
                                   bool preview, bool encode) noexcept {
  if (!i.initialized || i.telemetry.quarantined) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  const auto start = std::chrono::steady_clock::now();
  ApplePixelBufferFrame decoded{};
  ++i.telemetry.frames_requested;
  auto r = i.binding.acquire_decoder_frame(timestamp_us, decoded);
  if (r != DIGITOR_RESULT_OK || !decoded.pixel_buffer || !decoded.width || !decoded.height ||
      decoded.timestamp_us != timestamp_us) {
    ++i.telemetry.failures; i.telemetry.quarantined = true;
    i.telemetry.diagnostic = "VideoToolbox decoder surface acquisition failed";
    return r == DIGITOR_RESULT_OK ? DIGITOR_RESULT_INTERNAL_ERROR : r;
  }
  ++i.telemetry.pixel_buffers;
#if defined(__APPLE__)
  auto pb = static_cast<CVPixelBufferRef>(decoded.pixel_buffer);
  if (i.config.require_iosurface && !CVPixelBufferGetIOSurface(pb)) {
    ++i.telemetry.failures; i.telemetry.quarantined = true;
    i.telemetry.diagnostic = "CVPixelBuffer is not IOSurface-backed";
    return DIGITOR_RESULT_UNSUPPORTED;
  }
  if (CVPixelBufferGetPlaneCount(pb) != 2 ||
      CVPixelBufferGetWidth(pb) != decoded.width ||
      CVPixelBufferGetHeight(pb) != decoded.height) {
    ++i.telemetry.failures; i.telemetry.quarantined = true;
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
#endif
  AppleMetalImportedFrame imported{};
  r = i.binding.import_metal(decoded, imported);
  if (r != DIGITOR_RESULT_OK || !imported.luma_texture || !imported.chroma_texture ||
      imported.timestamp_us != timestamp_us ||
      imported.frame_identity != decoded.frame_identity) {
    ++i.telemetry.failures; i.telemetry.quarantined = true;
    i.telemetry.diagnostic = "CVMetalTextureCache import failed";
    return r == DIGITOR_RESULT_OK ? DIGITOR_RESULT_INTERNAL_ERROR : r;
  }
  ++i.telemetry.metal_imports;
  ProcessedGpuFramePtr graded;
  r = i.binding.convert_to_rgba16f(imported, graded);
  if (r != DIGITOR_RESULT_OK || !graded || !graded->ready() ||
      graded->metadata().format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT ||
      graded->metadata().timestamp != timestamp_us ||
      graded->identity() != decoded.frame_identity) {
    ++i.telemetry.failures; i.telemetry.quarantined = true;
    i.telemetry.diagnostic = "Metal conversion did not produce matching RGBA16F frame";
    return r == DIGITOR_RESULT_OK ? DIGITOR_RESULT_INTERNAL_ERROR : r;
  }
  ++i.telemetry.rgba16f_frames;
  if (preview) {
    r = i.binding.preview_consumer(graded);
    if (r != DIGITOR_RESULT_OK) { ++i.telemetry.failures; i.telemetry.quarantined = true; return r; }
    ++i.telemetry.preview_frames;
  }
  if (encode) {
    r = i.binding.encoder_consumer(graded);
    if (r != DIGITOR_RESULT_OK) { ++i.telemetry.failures; i.telemetry.quarantined = true; return r; }
    ++i.telemetry.encoder_frames;
  }
  if (std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start).count() > i.config.frame_timeout_ms) {
    ++i.telemetry.failures; i.telemetry.quarantined = true;
    i.telemetry.diagnostic = "Apple zero-copy frame deadline exceeded";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  i.telemetry.diagnostic = "Apple zero-copy frame completed";
  return DIGITOR_RESULT_OK;
}

DigitorResult AppleZeroCopyPipeline::preview(std::int64_t t) noexcept { return process_frame(*impl_, t, true, false); }
DigitorResult AppleZeroCopyPipeline::export_frame(std::int64_t t) noexcept { return process_frame(*impl_, t, false, true); }
DigitorResult AppleZeroCopyPipeline::preview_and_export(std::int64_t t) noexcept { return process_frame(*impl_, t, true, true); }
DigitorResult AppleZeroCopyPipeline::reset_quarantine() noexcept {
  std::scoped_lock lock(impl_->mutex); impl_->telemetry.quarantined=false; impl_->telemetry.diagnostic="quarantine reset"; return DIGITOR_RESULT_OK;
}
AppleZeroCopyTelemetry AppleZeroCopyPipeline::telemetry() const { std::scoped_lock lock(impl_->mutex); return impl_->telemetry; }
bool AppleZeroCopyPipeline::production_active() const noexcept { std::scoped_lock lock(impl_->mutex); return impl_->initialized && !impl_->telemetry.quarantined && impl_->telemetry.cpu_copies==0 && impl_->telemetry.cpu_fallback_frames==0; }

} // namespace digitor
