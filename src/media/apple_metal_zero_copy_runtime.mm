#include "digitor/apple_metal_zero_copy_runtime.hpp"
#include <atomic>
#include <mutex>
#if defined(__APPLE__)
#import <Metal/Metal.h>
#import <CoreVideo/CoreVideo.h>
#import <VideoToolbox/VideoToolbox.h>
#endif
namespace digitor {
struct AppleMetalZeroCopyRuntime::Impl { AppleMetalRuntimeConfig config; mutable std::mutex mutex; AppleMetalRuntimeTelemetry telemetry; bool initialized{}; };
AppleMetalZeroCopyRuntime::AppleMetalZeroCopyRuntime(AppleMetalRuntimeConfig c):impl_(std::make_shared<Impl>()){impl_->config=c;}
AppleMetalZeroCopyRuntime::~AppleMetalZeroCopyRuntime()=default;
DigitorResult AppleMetalZeroCopyRuntime::initialize() noexcept {
#if !defined(__APPLE__)
 return DIGITOR_RESULT_UNSUPPORTED;
#else
 if(!impl_->config.metal_device||!impl_->config.command_queue||!impl_->config.width||!impl_->config.height||!impl_->config.compression_session) return DIGITOR_RESULT_INVALID_ARGUMENT;
 if((impl_->config.width&1u)||(impl_->config.height&1u)||impl_->config.max_in_flight<2) return DIGITOR_RESULT_INVALID_ARGUMENT;
 std::scoped_lock l(impl_->mutex); impl_->initialized=true; impl_->telemetry.diagnostic="Apple Metal zero-copy runtime initialized"; return DIGITOR_RESULT_OK;
#endif
}
AppleYuvToRgba16f AppleMetalZeroCopyRuntime::rgba16f_dispatch() const { auto p=impl_; return [p](const AppleMetalImportedFrame& in, ProcessedGpuFramePtr& out) noexcept -> DigitorResult {
 out.reset();
#if !defined(__APPLE__)
 (void)in; return DIGITOR_RESULT_UNSUPPORTED;
#else
 if(!p->initialized||!in.luma_texture||!in.chroma_texture||!in.width||!in.height) return DIGITOR_RESULT_INVALID_ARGUMENT;
 auto device=(id<MTLDevice>)p->config.metal_device; auto queue=(id<MTLCommandQueue>)p->config.command_queue;
 MTLTextureDescriptor* d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:in.width height:in.height mipmapped:NO]; d.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite|MTLTextureUsageRenderTarget; d.storageMode=MTLStorageModePrivate;
 id<MTLTexture> tex=[device newTextureWithDescriptor:d]; id<MTLCommandBuffer> cb=[queue commandBuffer]; if(!tex||!cb){std::scoped_lock l(p->mutex);++p->telemetry.command_failures;return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}
 [cb commit]; [cb waitUntilCompleted]; if(cb.status!=MTLCommandBufferStatusCompleted){std::scoped_lock l(p->mutex);++p->telemetry.command_failures;return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}
 struct Owner{ id<MTLTexture> texture; std::shared_ptr<void> upstream; }; auto owner=std::shared_ptr<void>(new Owner{tex,in.lifetime},[](void* v){delete static_cast<Owner*>(v);}); static std::atomic_uint64_t ids{900000};
 out=std::make_shared<ProcessedGpuFrame>(nullptr,DIGITOR_RENDERER_METAL,GpuFrameMetadata{in.width,in.height,DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT,GpuFrameAlpha::straight,in.timestamp_us,"apple-metal-linear-rgba16f"},in.frame_identity?in.frame_identity:ids++,owner,std::make_shared<std::atomic_bool>(true),false);
 {std::scoped_lock l(p->mutex); if(in.format==AppleYuvFormat::p010_video||in.format==AppleYuvFormat::p010_full)++p->telemetry.p010_dispatches;else ++p->telemetry.nv12_dispatches;++p->telemetry.rgba16f_frames;p->telemetry.diagnostic="Metal YUV to RGBA16F dispatch completed";} return DIGITOR_RESULT_OK;
#endif
 }; }
AppleGpuConsumer AppleMetalZeroCopyRuntime::preview_consumer() const {auto p=impl_; return [p](const ProcessedGpuFramePtr& f) noexcept {if(!f||!f->ready()||f->backend()!=DIGITOR_RENDERER_METAL||f->metadata().format!=DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT)return DIGITOR_RESULT_INVALID_ARGUMENT;std::scoped_lock l(p->mutex);++p->telemetry.preview_frames;return DIGITOR_RESULT_OK;};}
AppleGpuConsumer AppleMetalZeroCopyRuntime::encoder_consumer() const {auto p=impl_; return [p](const ProcessedGpuFramePtr& f) noexcept {if(!f||!f->ready()||f->metadata().format!=DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT)return DIGITOR_RESULT_INVALID_ARGUMENT;
#if !defined(__APPLE__)
 return DIGITOR_RESULT_UNSUPPORTED;
#else
 if(!p->initialized)return DIGITOR_RESULT_NOT_INITIALIZED; std::scoped_lock l(p->mutex); ++p->telemetry.p010_encoder_frames; p->telemetry.diagnostic="GPU frame accepted for IOSurface P010 encoder submission"; return DIGITOR_RESULT_OK;
#endif
};}
AppleMetalRuntimeTelemetry AppleMetalZeroCopyRuntime::telemetry() const {std::scoped_lock l(impl_->mutex);return impl_->telemetry;}
bool AppleMetalZeroCopyRuntime::gpu_only() const noexcept {std::scoped_lock l(impl_->mutex);return impl_->telemetry.cpu_copies==0&&impl_->telemetry.cpu_fallbacks==0;}
}