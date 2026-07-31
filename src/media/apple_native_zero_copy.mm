#include "digitor/apple_native_zero_copy.hpp"

#include <mutex>
#include <utility>

#if defined(__APPLE__)
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#import <VideoToolbox/VideoToolbox.h>
#endif

namespace digitor {

struct AppleNativeZeroCopyBindings::Impl {
  AppleNativeZeroCopyConfig config;
  AppleMetalRgba16fDispatch dispatch;
  AppleEncoderPixelBufferAcquire acquire_encoder_buffer;
  mutable std::mutex mutex;
  AppleNativeZeroCopyTelemetry telemetry;
#if defined(__APPLE__)
  CVMetalTextureCacheRef texture_cache{};
#endif
};

AppleNativeZeroCopyBindings::AppleNativeZeroCopyBindings(
    AppleNativeZeroCopyConfig c, AppleMetalRgba16fDispatch d,
    AppleEncoderPixelBufferAcquire a)
    : impl_(std::make_shared<Impl>()) {
  impl_->config = std::move(c);
  impl_->dispatch = std::move(d);
  impl_->acquire_encoder_buffer = std::move(a);
}

AppleNativeZeroCopyBindings::~AppleNativeZeroCopyBindings() {
#if defined(__APPLE__)
  if (impl_ && impl_->texture_cache) CFRelease(impl_->texture_cache);
#endif
}

DigitorResult AppleNativeZeroCopyBindings::initialize() noexcept {
#if !defined(__APPLE__)
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  auto& i = *impl_;
  if (!i.config.metal_device || !i.config.command_queue || !i.dispatch ||
      !i.acquire_encoder_buffer || !i.config.compression_session)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  const auto status = CVMetalTextureCacheCreate(
      kCFAllocatorDefault, nullptr,
      (__bridge id<MTLDevice>)i.config.metal_device, nullptr,
      &i.texture_cache);
  if (status != kCVReturnSuccess || !i.texture_cache) {
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.failures;
    i.telemetry.diagnostic = "CVMetalTextureCacheCreate failed";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  std::scoped_lock lock(i.mutex);
  i.telemetry.diagnostic = "Apple native zero-copy bindings initialized";
  return DIGITOR_RESULT_OK;
#endif
}

DigitorResult AppleNativeZeroCopyBindings::import_metal(
    const ApplePixelBufferFrame& frame, AppleMetalImportedFrame& out) noexcept {
  out = {};
#if !defined(__APPLE__)
  (void)frame;
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  auto& i = *impl_;
  if (!i.texture_cache || !frame.pixel_buffer || !frame.width || !frame.height)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  auto pb = static_cast<CVPixelBufferRef>(frame.pixel_buffer);
  if (!CVPixelBufferGetIOSurface(pb) || CVPixelBufferGetPlaneCount(pb) != 2 ||
      CVPixelBufferGetWidth(pb) != frame.width ||
      CVPixelBufferGetHeight(pb) != frame.height)
    return DIGITOR_RESULT_UNSUPPORTED;

  const bool p010 = frame.format == AppleYuvFormat::p010_video ||
                    frame.format == AppleYuvFormat::p010_full;
  if (p010 != (frame.bit_depth == 10)) return DIGITOR_RESULT_INVALID_ARGUMENT;

  const MTLPixelFormat y_format = p010 ? MTLPixelFormatR16Unorm : MTLPixelFormatR8Unorm;
  const MTLPixelFormat uv_format = p010 ? MTLPixelFormatRG16Unorm : MTLPixelFormatRG8Unorm;
  CVMetalTextureRef y_ref{};
  CVMetalTextureRef uv_ref{};
  auto s = CVMetalTextureCacheCreateTextureFromImage(
      kCFAllocatorDefault, i.texture_cache, pb, nullptr, y_format,
      frame.width, frame.height, 0, &y_ref);
  if (s != kCVReturnSuccess || !y_ref) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  s = CVMetalTextureCacheCreateTextureFromImage(
      kCFAllocatorDefault, i.texture_cache, pb, nullptr, uv_format,
      frame.width / 2, frame.height / 2, 1, &uv_ref);
  if (s != kCVReturnSuccess || !uv_ref) {
    CFRelease(y_ref);
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  struct Lifetime {
    CVPixelBufferRef pb{};
    CVMetalTextureRef y{};
    CVMetalTextureRef uv{};
    ~Lifetime() {
      if (uv) CFRelease(uv);
      if (y) CFRelease(y);
      if (pb) CFRelease(pb);
    }
  };
  auto holder = std::make_shared<Lifetime>();
  holder->pb = pb;
  holder->y = y_ref;
  holder->uv = uv_ref;
  CFRetain(pb);

  out.luma_texture = (__bridge void*)CVMetalTextureGetTexture(y_ref);
  out.chroma_texture = (__bridge void*)CVMetalTextureGetTexture(uv_ref);
  out.width = frame.width;
  out.height = frame.height;
  out.format = frame.format;
  out.matrix = frame.matrix;
  out.timestamp_us = frame.timestamp_us;
  out.frame_identity = frame.frame_identity;
  out.lifetime = std::move(holder);

  std::scoped_lock lock(i.mutex);
  ++i.telemetry.texture_cache_imports;
  if (p010) ++i.telemetry.p010_imports; else ++i.telemetry.nv12_imports;
  i.telemetry.diagnostic = "CVPixelBuffer imported as Metal Y/UV textures";
  return DIGITOR_RESULT_OK;
#endif
}

DigitorResult AppleNativeZeroCopyBindings::convert(
    const AppleMetalImportedFrame& imported, ProcessedGpuFramePtr& out) noexcept {
  out.reset();
  auto& i = *impl_;
  const auto r = i.dispatch(imported, i.config.command_queue, out);
  if (r != DIGITOR_RESULT_OK || !out || !out->ready() ||
      out->backend() != DIGITOR_RENDERER_METAL ||
      out->metadata().format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT ||
      out->metadata().width != imported.width ||
      out->metadata().height != imported.height ||
      out->metadata().timestamp != imported.timestamp_us ||
      out->identity() != imported.frame_identity) {
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.failures;
    i.telemetry.diagnostic = "Metal dispatch produced invalid RGBA16F frame";
    out.reset();
    return r == DIGITOR_RESULT_OK ? DIGITOR_RESULT_INTERNAL_ERROR : r;
  }
  std::scoped_lock lock(i.mutex);
  ++i.telemetry.compute_dispatches;
  return DIGITOR_RESULT_OK;
}

DigitorResult AppleNativeZeroCopyBindings::submit_encoder(
    const ProcessedGpuFramePtr& frame) noexcept {
#if !defined(__APPLE__)
  (void)frame;
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  auto& i = *impl_;
  if (!frame || !frame->ready() || frame->backend() != DIGITOR_RENDERER_METAL ||
      frame->metadata().format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  void* raw{};
  auto r = i.acquire_encoder_buffer(frame, raw);
  if (r != DIGITOR_RESULT_OK || !raw) return r == DIGITOR_RESULT_OK ? DIGITOR_RESULT_INTERNAL_ERROR : r;
  auto pb = static_cast<CVPixelBufferRef>(raw);
  const auto pts = CMTimeMake(frame->metadata().timestamp, 1000000);
  const auto duration = CMTimeMake(i.config.frame_duration_us, 1000000);
  VTEncodeInfoFlags flags{};
  const auto status = VTCompressionSessionEncodeFrame(
      static_cast<VTCompressionSessionRef>(i.config.compression_session),
      pb, pts, duration, nullptr, nullptr, &flags);
  CFRelease(pb);
  if (status != noErr) {
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.failures;
    i.telemetry.diagnostic = "VTCompressionSessionEncodeFrame failed";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  std::scoped_lock lock(i.mutex);
  ++i.telemetry.encoder_submissions;
  i.telemetry.diagnostic = "VideoToolbox frame submitted";
  return DIGITOR_RESULT_OK;
#endif
}

AppleZeroCopyBinding AppleNativeZeroCopyBindings::binding(
    AppleVideoToolboxAcquire acquire, AppleGpuConsumer preview) {
  auto keep = impl_;
  AppleZeroCopyBinding b{};
  b.acquire_decoder_frame = std::move(acquire);
  b.import_metal = [keep](const ApplePixelBufferFrame& f,
                          AppleMetalImportedFrame& o) noexcept {
    AppleNativeZeroCopyBindings x(keep->config, keep->dispatch,
                                  keep->acquire_encoder_buffer);
    x.impl_ = keep;
    return x.import_metal(f, o);
  };
  b.convert_to_rgba16f = [keep](const AppleMetalImportedFrame& f,
                                ProcessedGpuFramePtr& o) noexcept {
    AppleNativeZeroCopyBindings x(keep->config, keep->dispatch,
                                  keep->acquire_encoder_buffer);
    x.impl_ = keep;
    return x.convert(f, o);
  };
  b.preview_consumer = std::move(preview);
  b.encoder_consumer = [keep](const ProcessedGpuFramePtr& f) noexcept {
    AppleNativeZeroCopyBindings x(keep->config, keep->dispatch,
                                  keep->acquire_encoder_buffer);
    x.impl_ = keep;
    return x.submit_encoder(f);
  };
  return b;
}

AppleNativeZeroCopyTelemetry AppleNativeZeroCopyBindings::telemetry() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->telemetry;
}

bool AppleNativeZeroCopyBindings::gpu_only() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return impl_->telemetry.cpu_copies == 0;
}

} // namespace digitor
