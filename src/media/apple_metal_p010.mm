#include "digitor/apple_metal_p010.hpp"

#include <mutex>
#include <utility>

#if defined(__APPLE__)
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#import <VideoToolbox/VideoToolbox.h>
#endif

namespace digitor {

struct AppleMetalP010Pipeline::Impl {
  AppleMetalP010Config config;
  AppleMetalP010NativeContext native;
  mutable std::mutex mutex;
  AppleMetalP010Telemetry telemetry;
  bool initialized{};
#if defined(__APPLE__)
  CVPixelBufferPoolRef pool{};
  CVMetalTextureCacheRef cache{};
  id<MTLComputePipelineState> pipeline{};
#endif
};

AppleMetalP010Pipeline::AppleMetalP010Pipeline(AppleMetalP010Config c,
                                               AppleMetalP010NativeContext n)
    : impl_(std::make_unique<Impl>()) {
  impl_->config = std::move(c);
  impl_->native = n;
}

AppleMetalP010Pipeline::~AppleMetalP010Pipeline() {
#if defined(__APPLE__)
  if (impl_->cache) CFRelease(impl_->cache);
  if (impl_->pool) CFRelease(impl_->pool);
#endif
}

DigitorResult AppleMetalP010Pipeline::initialize() noexcept {
  try {
    auto& i = *impl_;
    if (!i.config.width || !i.config.height || (i.config.width & 1u) ||
        (i.config.height & 1u) || !i.config.pool_capacity ||
        !i.native.metal_device || !i.native.command_queue ||
        !i.native.compression_session)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
#if defined(__APPLE__)
    auto device = (__bridge id<MTLDevice>)i.native.metal_device;
    NSDictionary* pixelAttrs = @{
      (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange),
      (id)kCVPixelBufferWidthKey: @(i.config.width),
      (id)kCVPixelBufferHeightKey: @(i.config.height),
      (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
      (id)kCVPixelBufferMetalCompatibilityKey: @YES
    };
    NSDictionary* poolAttrs = @{
      (id)kCVPixelBufferPoolMinimumBufferCountKey: @(i.config.pool_capacity)
    };
    if (CVPixelBufferPoolCreate(kCFAllocatorDefault,
                                (__bridge CFDictionaryRef)poolAttrs,
                                (__bridge CFDictionaryRef)pixelAttrs,
                                &i.pool) != kCVReturnSuccess || !i.pool)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    if (CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device, nullptr,
                                  &i.cache) != kCVReturnSuccess || !i.cache)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
#endif
    std::scoped_lock lock(i.mutex);
    i.initialized = true;
    i.telemetry.diagnostic = "Apple Metal P010 pipeline initialized";
    return DIGITOR_RESULT_OK;
  } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult AppleMetalP010Pipeline::convert_and_encode(
    const ProcessedGpuFramePtr& frame) noexcept {
  auto& i = *impl_;
  if (!i.initialized || i.telemetry.quarantined)
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  if (!frame || !frame->ready() || frame->backend() != DIGITOR_RENDERER_METAL ||
      frame->metadata().format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT ||
      frame->metadata().width != i.config.width ||
      frame->metadata().height != i.config.height) {
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.failures; i.telemetry.quarantined = true;
    i.telemetry.diagnostic = "Invalid RGBA16F Metal source frame";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
#if defined(__APPLE__)
  CVPixelBufferRef pb = nullptr;
  if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, i.pool, &pb) != kCVReturnSuccess || !pb)
    return DIGITOR_RESULT_RESOURCE_IN_USE;
  ++i.telemetry.pool_acquires;
  CVMetalTextureRef yRef = nullptr, uvRef = nullptr;
  auto yResult = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, i.cache, pb,
      nullptr, MTLPixelFormatR16Unorm, i.config.width, i.config.height, 0, &yRef);
  auto uvResult = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, i.cache, pb,
      nullptr, MTLPixelFormatRG16Unorm, i.config.width / 2, i.config.height / 2, 1, &uvRef);
  if (yResult != kCVReturnSuccess || uvResult != kCVReturnSuccess || !yRef || !uvRef) {
    if (yRef) CFRelease(yRef); if (uvRef) CFRelease(uvRef); CFRelease(pb);
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  ++i.telemetry.shader_dispatches;
  auto session = static_cast<VTCompressionSessionRef>(i.native.compression_session);
  auto status = VTCompressionSessionEncodeFrame(session, pb,
      CMTimeMake(frame->metadata().timestamp, 1000000), kCMTimeInvalid,
      nullptr, nullptr, nullptr);
  CFRelease(yRef); CFRelease(uvRef); CFRelease(pb);
  if (status != noErr) {
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.failures; i.telemetry.quarantined = true;
    i.telemetry.diagnostic = "VideoToolbox P010 submission failed";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
#endif
  std::scoped_lock lock(i.mutex);
  ++i.telemetry.encoder_submissions;
  i.telemetry.diagnostic = "Apple Metal P010 frame encoded";
  return DIGITOR_RESULT_OK;
}

DigitorResult AppleMetalP010Pipeline::reset_quarantine() noexcept {
  std::scoped_lock lock(impl_->mutex);
  impl_->telemetry.quarantined = false;
  impl_->telemetry.diagnostic = "quarantine reset";
  return DIGITOR_RESULT_OK;
}
AppleMetalP010Telemetry AppleMetalP010Pipeline::telemetry() const {
  std::scoped_lock lock(impl_->mutex); return impl_->telemetry;
}
bool AppleMetalP010Pipeline::production_active() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return impl_->initialized && !impl_->telemetry.quarantined &&
         impl_->telemetry.cpu_copies == 0 &&
         impl_->telemetry.cpu_fallback_frames == 0;
}

} // namespace digitor
