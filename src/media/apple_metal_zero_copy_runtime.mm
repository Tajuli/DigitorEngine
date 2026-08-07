#include "digitor/apple_metal_zero_copy_runtime.hpp"

#include <mutex>
#include <utility>

namespace digitor {

struct AppleMetalZeroCopyRuntime::Impl {
  AppleMetalRuntimeConfig config;
  mutable std::mutex mutex;
  AppleMetalRuntimeTelemetry telemetry;
  bool initialized{};
};

AppleMetalZeroCopyRuntime::AppleMetalZeroCopyRuntime(AppleMetalRuntimeConfig c)
    : impl_(std::make_shared<Impl>()) {
  impl_->config = std::move(c);
}
AppleMetalZeroCopyRuntime::~AppleMetalZeroCopyRuntime() = default;

DigitorResult AppleMetalZeroCopyRuntime::initialize() noexcept {
#if !defined(__APPLE__)
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  if (!impl_->config.metal_device || !impl_->config.command_queue ||
      !impl_->config.width || !impl_->config.height ||
      !impl_->config.compression_session || !impl_->config.yuv_to_rgba16f ||
      !impl_->config.preview_present || !impl_->config.encoder_submit)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  if ((impl_->config.width & 1u) || (impl_->config.height & 1u) ||
      impl_->config.max_in_flight < 2)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  std::scoped_lock lock(impl_->mutex);
  impl_->initialized = true;
  impl_->telemetry.diagnostic = "Apple Metal zero-copy runtime initialized with native bindings";
  return DIGITOR_RESULT_OK;
#endif
}

AppleYuvToRgba16f AppleMetalZeroCopyRuntime::rgba16f_dispatch() const {
  auto p = impl_;
  return [p](const AppleMetalImportedFrame& in,
             ProcessedGpuFramePtr& out) noexcept -> DigitorResult {
    out.reset();
#if !defined(__APPLE__)
    (void)in;
    return DIGITOR_RESULT_UNSUPPORTED;
#else
    if (!p->initialized || !p->config.yuv_to_rgba16f ||
        !in.luma_texture || !in.chroma_texture || !in.width || !in.height)
      return DIGITOR_RESULT_INVALID_ARGUMENT;

    const auto result = p->config.yuv_to_rgba16f(in, out);
    if (result != DIGITOR_RESULT_OK) {
      std::scoped_lock lock(p->mutex);
      ++p->telemetry.command_failures;
      p->telemetry.diagnostic = "native Metal YUV conversion failed";
      out.reset();
      return result;
    }
    if (!out || !out->ready() || !out->context_live() ||
        out->backend() != DIGITOR_RENDERER_METAL ||
        out->metadata().format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT ||
        out->metadata().width != in.width || out->metadata().height != in.height ||
        out->metadata().timestamp != in.timestamp_us ||
        (in.frame_identity != 0 && out->identity() != in.frame_identity)) {
      std::scoped_lock lock(p->mutex);
      ++p->telemetry.command_failures;
      p->telemetry.diagnostic = "native Metal conversion violated RGBA16F frame contract";
      out.reset();
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }

    std::scoped_lock lock(p->mutex);
    if (in.format == AppleYuvFormat::p010_video ||
        in.format == AppleYuvFormat::p010_full)
      ++p->telemetry.p010_dispatches;
    else
      ++p->telemetry.nv12_dispatches;
    ++p->telemetry.rgba16f_frames;
    p->telemetry.diagnostic = "native Metal YUV to RGBA16F dispatch completed";
    return DIGITOR_RESULT_OK;
#endif
  };
}

AppleGpuConsumer AppleMetalZeroCopyRuntime::preview_consumer() const {
  auto p = impl_;
  return [p](const ProcessedGpuFramePtr& frame) noexcept -> DigitorResult {
    if (!frame || !frame->ready() || !frame->context_live() ||
        frame->backend() != DIGITOR_RENDERER_METAL ||
        frame->metadata().format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
#if !defined(__APPLE__)
    return DIGITOR_RESULT_UNSUPPORTED;
#else
    if (!p->initialized || !p->config.preview_present)
      return DIGITOR_RESULT_NOT_INITIALIZED;
    const auto result = p->config.preview_present(frame);
    if (result != DIGITOR_RESULT_OK) {
      std::scoped_lock lock(p->mutex);
      ++p->telemetry.command_failures;
      p->telemetry.diagnostic = "native Metal preview presentation failed";
      return result;
    }
    std::scoped_lock lock(p->mutex);
    ++p->telemetry.preview_frames;
    p->telemetry.diagnostic = "native Metal preview frame presented";
    return DIGITOR_RESULT_OK;
#endif
  };
}

AppleGpuConsumer AppleMetalZeroCopyRuntime::encoder_consumer() const {
  auto p = impl_;
  return [p](const ProcessedGpuFramePtr& frame) noexcept -> DigitorResult {
    if (!frame || !frame->ready() || !frame->context_live() ||
        frame->backend() != DIGITOR_RENDERER_METAL ||
        frame->metadata().format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
#if !defined(__APPLE__)
    return DIGITOR_RESULT_UNSUPPORTED;
#else
    if (!p->initialized || !p->config.encoder_submit)
      return DIGITOR_RESULT_NOT_INITIALIZED;
    const auto result = p->config.encoder_submit(frame);
    if (result != DIGITOR_RESULT_OK) {
      std::scoped_lock lock(p->mutex);
      ++p->telemetry.command_failures;
      p->telemetry.diagnostic = "native VideoToolbox encoder submission failed";
      return result;
    }
    std::scoped_lock lock(p->mutex);
    ++p->telemetry.p010_encoder_frames;
    p->telemetry.diagnostic = "native GPU frame submitted to VideoToolbox encoder";
    return DIGITOR_RESULT_OK;
#endif
  };
}

AppleMetalRuntimeTelemetry AppleMetalZeroCopyRuntime::telemetry() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->telemetry;
}

bool AppleMetalZeroCopyRuntime::gpu_only() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return impl_->telemetry.cpu_copies == 0 && impl_->telemetry.cpu_fallbacks == 0;
}

}  // namespace digitor
