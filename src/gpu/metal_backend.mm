#import <Metal/Metal.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>

#include "core/numeric_utils.hpp"
#include "core/string_utils.hpp"
#include "gpu/gpu_backend.hpp"
#include "gpu/native_pipeline_cache.hpp"
#include "digitor/native_node_mask_backend.hpp"
#include "digitor/native_node_shader_contracts.hpp"

namespace digitor {
namespace {
struct MetalEncoderGuard {
  id<MTLCommandEncoder> encoder{nil};
  bool ended{};
  explicit MetalEncoderGuard(id<MTLCommandEncoder> value) noexcept
      : encoder(value) {}
  MetalEncoderGuard(const MetalEncoderGuard &) = delete;
  MetalEncoderGuard &operator=(const MetalEncoderGuard &) = delete;
  ~MetalEncoderGuard() {
    if (encoder != nil && !ended) {
      @try {
        [encoder endEncoding];
      } @catch (...) {
      }
    }
  }
  bool finish(bool injected) noexcept {
    if (encoder == nil)
      return false;
    @try {
      [encoder endEncoding];
      ended = true;
      return !injected;
    } @catch (...) {
      ended = true;
      return false;
    }
  }
};
struct MetalLiveResources {
  std::atomic<std::int64_t> textures{}, buffers{}, queues{}, commands{},
      encoders{}, pipelines{}, owners{}, consumers{};
  NativeResourceCounts snapshot() const noexcept {
    NativeResourceCounts n;
    n.images = textures.load();
    n.buffers = buffers.load();
    n.command_resources = queues.load() + commands.load() + encoders.load();
    n.pipelines = pipelines.load();
    n.frame_owners = owners.load();
    n.consumer_destinations = consumers.load();
    return n;
  }
} metal_live;
struct MetalPreviewOwner {
  id<MTLTexture> output;
  id<MTLTexture> preview;
  id<MTLCommandQueue> queue;
  std::shared_ptr<void> upstream;
  std::int64_t textures{}, queues{};
  bool tracked{};
  ~MetalPreviewOwner() {
    metal_live.textures -= textures;
    metal_live.queues -= queues;
    if (tracked)
      --metal_live.owners;
  }
};
struct MetalMatteOwner {
  id<MTLTexture> texture;
  std::vector<std::shared_ptr<void>> upstream;
  bool tracked{};
  ~MetalMatteOwner() {
    if (texture) --metal_live.textures;
    if (tracked) --metal_live.owners;
  }
};
struct MetalUpstreamBundle { std::vector<std::shared_ptr<void>> values; };
struct MetalConsumerOwner {
  id<MTLDevice> device;
  id<MTLTexture> texture;
  id<MTLCommandQueue> queue;
  bool tracked{};
  ~MetalConsumerOwner() {
    if (texture)
      --metal_live.textures;
    if (queue)
      --metal_live.queues;
    if (tracked)
      --metal_live.consumers;
  }
};
struct MetalPipelineBundle {
  id<MTLLibrary> library;
  id<MTLFunction> function;
  id<MTLComputePipelineState> pipeline;
  ~MetalPipelineBundle() {
    metal_live.pipelines -=
        (library ? 1 : 0) + (function ? 1 : 0) + (pipeline ? 1 : 0);
  }
};

MTLPixelFormat metal_format(DigitorPixelFormat format) noexcept {
  switch (format) {
  case DIGITOR_PIXEL_FORMAT_RGBA8_UNORM:
    return MTLPixelFormatRGBA8Unorm;
  case DIGITOR_PIXEL_FORMAT_BGRA8_UNORM:
    return MTLPixelFormatBGRA8Unorm;
  case DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT:
    return MTLPixelFormatRGBA16Float;
  case DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT:
    return MTLPixelFormatRGBA32Float;
  default:
    return MTLPixelFormatInvalid;
  }
}

// Resource creation uses __bridge_retained exactly once. This is its matching
// transfer back to ARC, which releases the object at the end of the scope.
void release_native(void *object) noexcept {
  if (object != nullptr) {
    (void)CFBridgingRelease(object);
  }
}

class MetalBackend final : public IRenderBackend, public NativeNodeMaskBackend {
  struct LocalCounts {
    std::int64_t textures{}, buffers{}, queues{}, commands{}, encoders{};
    ~LocalCounts() {
      metal_live.textures -= textures;
      metal_live.buffers -= buffers;
      metal_live.queues -= queues;
      metal_live.commands -= commands;
      metal_live.encoders -= encoders;
    }
    void transfer(MetalPreviewOwner &o) {
      o.textures += 2;
      o.queues += 1;
      textures -= 2;
      queues -= 1;
    }
  };
  class QualificationScope {
  public:
    QualificationScope(MetalBackend &b, const char *path)
        : b_(b), before_(metal_live.snapshot()),
          cache_(b.pipeline_cache_.counters()),
          primary_(primary_wheels_reference_count()),
          curves_(cpu_curve_reference_count()) {
      b_.begin_grade_provenance(DIGITOR_RENDERER_METAL, true,
                                b_.info_.device_name, "Metal runtime compiler",
                                "metal-native-stage", "metal-native-pipeline");
      b_.provenance_.requested_failure_point = gpu_failure_point();
      b_.provenance_.failure_path = path ? path : "";
      b_.provenance_.resources_before = before_;
      b_.provenance_.cache_before = cache_;
    }
    ~QualificationScope() {
      auto &p = b_.provenance_;
      p.resources_after = metal_live.snapshot();
      p.cache_after = b_.pipeline_cache_.counters();
      auto a = p.resources_after, z = before_;
      a.pipelines = z.pipelines = 0;
      p.cleanup_baseline =
          p.failure_result == DIGITOR_RESULT_OK ||
          (a == z &&
           p.resources_after.pipelines - before_.pipelines <=
               std::int64_t(p.cache_after.creations - cache_.creations) * 3);
      p.cache_valid =
          p.cache_after.hits >= cache_.hits &&
          p.cache_after.creations >= cache_.creations &&
          p.cache_after.creation_failures >= cache_.creation_failures;
      p.cpu_primary_wheels_invocations =
          primary_wheels_reference_count() - primary_;
      p.cpu_curve_invocations = cpu_curve_reference_count() - curves_;
      p.output_cleared =
          p.failure_result == DIGITOR_RESULT_OK || !p.output_written;
      p.recovery_succeeded = p.failure_result == DIGITOR_RESULT_OK;
    }

  private:
    MetalBackend &b_;
    NativeResourceCounts before_;
    NativePipelineCacheCounters cache_;
    std::uint64_t primary_, curves_;
  };
  bool fail(GpuFailurePoint p, const char *op) noexcept {
    return inject_at(p, op) != DIGITOR_RESULT_OK;
  }
  bool allocation_fail(GpuFailurePoint p, const char *op) noexcept {
    return fail(GpuFailurePoint::DeterministicOutOfMemory, op) || fail(p, op);
  }
  id<MTLTexture> make_texture(GpuFailurePoint p, const char *op,
                              MTLTextureDescriptor *d,
                              LocalCounts &c) noexcept {
    @try {
      if (allocation_fail(p, op))
        return nil;
      id<MTLTexture> x = [device_ newTextureWithDescriptor:d];
      if (x) {
        ++metal_live.textures;
        ++c.textures;
      }
      return x;
    } @catch (...) {
      return nil;
    }
  }
  id<MTLBuffer> make_buffer_bytes(GpuFailurePoint p, const char *op,
                                  const void *bytes, NSUInteger length,
                                  LocalCounts &c) noexcept {
    @try {
      if (allocation_fail(p, op))
        return nil;
      id<MTLBuffer> x =
          [device_ newBufferWithBytes:bytes
                               length:length
                              options:MTLResourceStorageModeShared];
      if (x) {
        ++metal_live.buffers;
        ++c.buffers;
      }
      return x;
    } @catch (...) {
      return nil;
    }
  }
  id<MTLBuffer> make_buffer_length(GpuFailurePoint p, const char *op,
                                   NSUInteger length, LocalCounts &c) noexcept {
    @try {
      if (allocation_fail(p, op))
        return nil;
      id<MTLBuffer> x =
          [device_ newBufferWithLength:length
                               options:MTLResourceStorageModeShared];
      if (x) {
        ++metal_live.buffers;
        ++c.buffers;
      }
      return x;
    } @catch (...) {
      return nil;
    }
  }
  id<MTLCommandQueue> make_queue(const char *op, LocalCounts &c) noexcept {
    @try {
      if (allocation_fail(GpuFailurePoint::CommandQueueCreation, op))
        return nil;
      id<MTLCommandQueue> x = [device_ newCommandQueue];
      if (x) {
        ++metal_live.queues;
        ++c.queues;
      }
      return x;
    } @catch (...) {
      return nil;
    }
  }
  id<MTLCommandBuffer> make_command(id<MTLCommandQueue> q, const char *op,
                                    LocalCounts &c) noexcept {
    @try {
      if (fail(GpuFailurePoint::CommandBufferOrListAllocation, op))
        return nil;
      id<MTLCommandBuffer> x = [q commandBuffer];
      if (x) {
        ++metal_live.commands;
        ++c.commands;
      }
      return x;
    } @catch (...) {
      return nil;
    }
  }
  id<MTLComputeCommandEncoder> make_compute_encoder(id<MTLCommandBuffer> c,
                                                    LocalCounts &n) noexcept {
    @try {
      if (fail(GpuFailurePoint::ComputeEncoderCreation,
               "computeCommandEncoder"))
        return nil;
      id<MTLComputeCommandEncoder> x = [c computeCommandEncoder];
      if (x) {
        ++metal_live.encoders;
        ++n.encoders;
      }
      return x;
    } @catch (...) {
      return nil;
    }
  }
  id<MTLBlitCommandEncoder> make_blit_encoder(id<MTLCommandBuffer> c,
                                              const char *op,
                                              LocalCounts &n) noexcept {
    @try {
      if (fail(GpuFailurePoint::BlitEncoderCreation, op))
        return nil;
      id<MTLBlitCommandEncoder> x = [c blitCommandEncoder];
      if (x) {
        ++metal_live.encoders;
        ++n.encoders;
      }
      return x;
    } @catch (...) {
      return nil;
    }
  }
  bool end_encoder(MetalEncoderGuard &guard, const char *op) noexcept {
    const bool injected = fail(GpuFailurePoint::EncoderCompletion, op);
    return guard.finish(injected);
  }
  DigitorResult abort_encoder(MetalEncoderGuard &guard,
                              DigitorResult result) noexcept {
    (void)guard.finish(false);
    return result;
  }
  bool commit_and_wait(id<MTLCommandBuffer> c) noexcept {
    @try {
      if (fail(GpuFailurePoint::QueueSubmission, "MTLCommandBuffer commit"))
        return false;
      [c commit];
      if (fail(GpuFailurePoint::SynchronizationWait,
               "MTLCommandBuffer waitUntilCompleted")) {
        [c waitUntilCompleted];
        return false;
      }
      [c waitUntilCompleted];
      if (fail(GpuFailurePoint::CommandStatusVerification,
               "MTLCommandBuffer status verification"))
        return false;
      return c.status == MTLCommandBufferStatusCompleted;
    } @catch (...) {
      return false;
    }
  }


  struct MetalHslConstants {
    float hue[4], saturation[4], luminance[4];
    float clean_black, clean_white, denoise, blur;
    std::uint32_t invert, width, height, padding;
  };
  static_assert(sizeof(MetalHslConstants) == 80);
  struct MetalWindowConstants {
    float center_x, center_y, width_f, height_f;
    float rotation, feather, opacity;
    std::uint32_t shape, invert, width, height, padding;
  };
  static_assert(sizeof(MetalWindowConstants) == 48);
  struct MetalSizeConstants { std::uint32_t width, height; };

  id<MTLTexture> make_node_texture(MTLPixelFormat format,
                                   std::uint32_t width,
                                   std::uint32_t height,
                                   LocalCounts& local) noexcept {
    MTLTextureDescriptor* d = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:format width:width height:height
                                  mipmapped:NO];
    d.storageMode = MTLStorageModePrivate;
    d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    return make_texture(GpuFailurePoint::OutputResourceCreation,
                        "newTextureWithDescriptor(native node)", d, local);
  }

  DigitorResult dispatch_node_msl(NativeNodeKernel kernel,
                                  std::uint32_t width,
                                  std::uint32_t height,
                                  std::span<id<MTLTexture> const> textures,
                                  const void* constants,
                                  std::size_t constant_bytes) noexcept {
    const auto contract = native_node_pipeline_contract(DIGITOR_RENDERER_METAL, kernel);
    if (!validate_native_node_pipeline_contract(contract) ||
        !constants || constant_bytes != contract.constant_bytes)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    @autoreleasepool {
      @try {
        NSString* source = [[NSString alloc]
            initWithBytes:contract.source.data()
                   length:contract.source.size()
                 encoding:NSUTF8StringEncoding];
        NSString* entry = [[NSString alloc]
            initWithBytes:contract.entry_point.data()
                   length:contract.entry_point.size()
                 encoding:NSUTF8StringEncoding];
        NSError* error = nil;
        id<MTLLibrary> library = [device_ newLibraryWithSource:source options:nil error:&error];
        id<MTLFunction> function = library ? [library newFunctionWithName:entry] : nil;
        id<MTLComputePipelineState> pipeline = function
            ? [device_ newComputePipelineStateWithFunction:function error:&error]
            : nil;
        if (!pipeline) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        LocalCounts local;
        id<MTLCommandQueue> queue = make_queue("newCommandQueue(native node)", local);
        id<MTLCommandBuffer> command = queue
            ? make_command(queue, "commandBuffer(native node)", local) : nil;
        id<MTLComputeCommandEncoder> encoder = command
            ? make_compute_encoder(command, local) : nil;
        MetalEncoderGuard guard(encoder);
        if (!queue || !command || !encoder)
          return abort_encoder(guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [encoder setComputePipelineState:pipeline];
        for (NSUInteger i = 0; i < textures.size(); ++i)
          [encoder setTexture:textures[i] atIndex:i];
        [encoder setBytes:constants length:constant_bytes atIndex:0];
        [encoder dispatchThreads:MTLSizeMake(width, height, 1)
            threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
        if (!end_encoder(guard, "native node endEncoding") ||
            !commit_and_wait(command))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        return DIGITOR_RESULT_OK;
      } @catch (...) {
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
    }
  }

public:
  BackendProductionCapability production_capability() const noexcept override {
    BackendProductionCapability out{};
    out.backend = DIGITOR_RENDERER_METAL;
    out.context_identity = backend_context_identity();
    out.frame_context_identity = this;
    out.resources = MetalProductionResources{(__bridge void*)device_};
    return out;
  }
  [[nodiscard]] NativeNodeMaskCapabilities
  native_node_mask_capabilities() const noexcept override {
    return {true, true, true, true};
  }

  DigitorResult generate_hsl_matte(
      const GpuSourceResource& source, std::int64_t timestamp,
      const HslQualifierParameters& parameters,
      GpuMatteResourcePtr& output) noexcept override {
    output.reset();
    if (!source.usable_by(DIGITOR_RENDERER_METAL, backend_context_identity()))
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto prior = std::static_pointer_cast<MetalPreviewOwner>(native_owner(*source.frame));
    if (!prior || !prior->output) return DIGITOR_RESULT_INVALID_ARGUMENT;
    LocalCounts local;
    id<MTLTexture> texture = make_node_texture(MTLPixelFormatR32Float,
                                                source.width, source.height, local);
    if (!texture) return DIGITOR_RESULT_OUT_OF_MEMORY;
    const auto& values = parameters.values();
    MetalHslConstants c{};
    const auto set_range = [](float (&target)[4], const QualifierRange& range) {
      target[0] = range.low; target[1] = range.high;
      target[2] = range.softness; target[3] = 0.0f;
    };
    set_range(c.hue, values.hue); set_range(c.saturation, values.saturation);
    set_range(c.luminance, values.luminance);
    c.clean_black = values.clean_black; c.clean_white = values.clean_white;
    c.denoise = values.denoise; c.blur = values.blur;
    c.invert = values.invert ? 1u : 0u; c.width = source.width; c.height = source.height;
    const id<MTLTexture> textures[]{prior->output, texture};
    auto status = dispatch_node_msl(NativeNodeKernel::hsl_matte, source.width,
                                    source.height, textures, &c, sizeof(c));
    if (status != DIGITOR_RESULT_OK) return status;
    auto owner = std::make_shared<MetalMatteOwner>();
    owner->texture = texture; owner->upstream.push_back(prior); owner->tracked = true;
    ++metal_live.owners; --local.textures;
    static std::atomic_uint64_t ids{800000};
    output = std::make_shared<GpuMatteResource>(
        DIGITOR_RENDERER_METAL, backend_context_identity(),
        GpuMatteMetadata{source.width, source.height, timestamp, GpuMatteFormat::r32_float},
        ids++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), backend_context_lifetime());
    return DIGITOR_RESULT_OK;
  }

  DigitorResult generate_power_window_matte(
      std::uint32_t width, std::uint32_t height, std::int64_t timestamp,
      const PowerWindowSettings& settings,
      GpuMatteResourcePtr& output) noexcept override {
    output.reset();
    if (!width || !height) return DIGITOR_RESULT_INVALID_ARGUMENT;
    LocalCounts local;
    id<MTLTexture> texture = make_node_texture(MTLPixelFormatR32Float, width, height, local);
    if (!texture) return DIGITOR_RESULT_OUT_OF_MEMORY;
    MetalWindowConstants c{settings.center_x, settings.center_y, settings.width,
      settings.height, settings.rotation, settings.feather, settings.opacity,
      static_cast<std::uint32_t>(settings.shape), settings.invert ? 1u : 0u,
      width, height, 0u};
    const id<MTLTexture> textures[]{texture};
    auto status = dispatch_node_msl(NativeNodeKernel::power_window_matte,
                                    width, height, textures, &c, sizeof(c));
    if (status != DIGITOR_RESULT_OK) return status;
    auto owner = std::make_shared<MetalMatteOwner>();
    owner->texture = texture; owner->tracked = true; ++metal_live.owners; --local.textures;
    static std::atomic_uint64_t ids{900000};
    output = std::make_shared<GpuMatteResource>(
        DIGITOR_RENDERER_METAL, backend_context_identity(),
        GpuMatteMetadata{width, height, timestamp, GpuMatteFormat::r32_float},
        ids++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), backend_context_lifetime());
    return DIGITOR_RESULT_OK;
  }

  DigitorResult multiply_mattes(
      std::span<const GpuMatteResourcePtr> inputs, std::int64_t timestamp,
      GpuMatteResourcePtr& output) noexcept override {
    output.reset();
    if (inputs.empty()) return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (inputs.size() == 1) { output = inputs.front(); return DIGITOR_RESULT_OK; }
    GpuMatteResourcePtr current = inputs.front();
    for (std::size_t i = 1; i < inputs.size(); ++i) {
      const auto& rhs = inputs[i];
      if (!current || !rhs ||
          !current->usable_by(DIGITOR_RENDERER_METAL, backend_context_identity()) ||
          !rhs->usable_by(DIGITOR_RENDERER_METAL, backend_context_identity()) ||
          current->metadata().width != rhs->metadata().width ||
          current->metadata().height != rhs->metadata().height)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      auto left = std::static_pointer_cast<MetalMatteOwner>(current->native_owner());
      auto right = std::static_pointer_cast<MetalMatteOwner>(rhs->native_owner());
      LocalCounts local;
      id<MTLTexture> texture = make_node_texture(MTLPixelFormatR32Float,
          current->metadata().width, current->metadata().height, local);
      if (!texture) return DIGITOR_RESULT_OUT_OF_MEMORY;
      MetalSizeConstants c{current->metadata().width, current->metadata().height};
      const id<MTLTexture> textures[]{left->texture, right->texture, texture};
      auto status = dispatch_node_msl(NativeNodeKernel::matte_multiply,
          c.width, c.height, textures, &c, sizeof(c));
      if (status != DIGITOR_RESULT_OK) return status;
      auto owner = std::make_shared<MetalMatteOwner>();
      owner->texture = texture; owner->upstream = {left, right}; owner->tracked = true;
      ++metal_live.owners; --local.textures;
      static std::atomic_uint64_t ids{1000000};
      current = std::make_shared<GpuMatteResource>(
          DIGITOR_RENDERER_METAL, backend_context_identity(),
          GpuMatteMetadata{c.width, c.height, timestamp, GpuMatteFormat::r32_float},
          ids++, std::static_pointer_cast<void>(owner),
          std::make_shared<std::atomic_bool>(true), backend_context_lifetime());
    }
    output = std::move(current);
    return DIGITOR_RESULT_OK;
  }

  DigitorResult composite_with_matte(
      const GpuSourceResource& original, const GpuSourceResource& processed,
      const GpuMatteResourcePtr& matte, std::int64_t timestamp,
      ProcessedGpuFramePtr& output) noexcept override {
    output.reset();
    if (!original.usable_by(DIGITOR_RENDERER_METAL, backend_context_identity()) ||
        !processed.usable_by(DIGITOR_RENDERER_METAL, backend_context_identity()) ||
        !matte || !matte->usable_by(DIGITOR_RENDERER_METAL, backend_context_identity()) ||
        original.width != processed.width || original.height != processed.height ||
        original.width != matte->metadata().width || original.height != matte->metadata().height)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto a = std::static_pointer_cast<MetalPreviewOwner>(native_owner(*original.frame));
    auto b = std::static_pointer_cast<MetalPreviewOwner>(native_owner(*processed.frame));
    auto m = std::static_pointer_cast<MetalMatteOwner>(matte->native_owner());
    LocalCounts local;
    id<MTLTexture> result = make_node_texture(MTLPixelFormatRGBA32Float,
                                               original.width, original.height, local);
    if (!result) return DIGITOR_RESULT_OUT_OF_MEMORY;
    MetalSizeConstants c{original.width, original.height};
    const id<MTLTexture> textures[]{a->output, b->output, m->texture, result};
    auto status = dispatch_node_msl(NativeNodeKernel::masked_composite,
                                    c.width, c.height, textures, &c, sizeof(c));
    if (status != DIGITOR_RESULT_OK) return status;
    auto bundle = std::make_shared<MetalUpstreamBundle>();
    bundle->values = {a, b, m};
    auto owner = std::make_shared<MetalPreviewOwner>();
    owner->output = result; owner->preview = nil; owner->queue = nil;
    owner->upstream = bundle; owner->textures = 1; owner->tracked = true;
    ++metal_live.owners; --local.textures;
    static std::atomic_uint64_t ids{1100000};
    output = std::make_shared<ProcessedGpuFrame>(
        this, DIGITOR_RENDERER_METAL,
        GpuFrameMetadata{original.width, original.height,
                         DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
                         GpuFrameAlpha::straight, timestamp,
                         original.color_metadata_identity},
        ids++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), true);
    bind_frame_context_lifetime(output);
    return DIGITOR_RESULT_OK;
  }

  explicit MetalBackend(id<MTLDevice> device) : device_(device) {
    info_.backend = DIGITOR_RENDERER_METAL;
    copy_bounded(info_.backend_name, "Metal");
    copy_bounded(info_.device_name,
                 device.name.UTF8String != nullptr
                     ? std::string_view(device.name.UTF8String)
                     : std::string_view{});
    info_.is_gpu = 1;
    info_.supports_compute = 1;
    info_.supports_fp16 = 1;
    info_.supports_fp32 = 1;
  }
  std::shared_ptr<MetalPipelineBundle> color_pipeline(int operation,
                                                      NSString *code) noexcept {
    const bool curves = operation == 1;
    const bool log_wheels = operation == 2;
    const bool hsl_qualifier = operation == 3;
    std::string identity = curves ? "rgb-curves:metal-v1"
                                  : log_wheels ? "log-wheels:metal-v1"
                                  : hsl_qualifier ? "hsl-qualifier:metal-v1"
                                                  : "primary-wheels:metal-v1";
    const auto requested = gpu_failure_point();
    if (requested == GpuFailurePoint::LibraryCreation ||
        requested == GpuFailurePoint::ShaderFunctionLookup ||
        requested == GpuFailurePoint::PipelineCreation)
      identity +=
          ":injected-create:" + std::string(gpu_failure_point_name(requested));
    NativePipelineCacheKey key{
        DIGITOR_RENDERER_METAL,
        reinterpret_cast<std::uintptr_t>((__bridge void *)device_),
        identity,
        1,
        GpuPrecisionMode::Float32,
        DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT};
    return std::static_pointer_cast<
        MetalPipelineBundle>(pipeline_cache_.get_or_create(
        key, [&]() -> NativePipelineCache::Object {
          @autoreleasepool {
            @try {
              if (inject_at(GpuFailurePoint::LibraryCreation,
                            "newLibraryWithSource") != DIGITOR_RESULT_OK)
                return {};
              NSError *error = nil;
              auto bundle = std::make_shared<MetalPipelineBundle>();
              bundle->library = [device_ newLibraryWithSource:code
                                                      options:nil
                                                        error:&error];
              if (bundle->library)
                ++metal_live.pipelines;
              if (inject_at(GpuFailurePoint::ShaderFunctionLookup,
                            "newFunctionWithName") != DIGITOR_RESULT_OK)
                return {};
              bundle->function = [bundle->library
                  newFunctionWithName:curves ? @"curves" : log_wheels ? @"log_wheels" : hsl_qualifier ? @"hsl_qualifier" : @"wheels"];
              if (bundle->function)
                ++metal_live.pipelines;
              if (inject_at(GpuFailurePoint::PipelineCreation,
                            "newComputePipelineStateWithFunction") !=
                  DIGITOR_RESULT_OK)
                return {};
              bundle->pipeline =
                  bundle->function
                      ? [device_
                            newComputePipelineStateWithFunction:bundle->function
                                                          error:&error]
                      : nil;
              if (bundle->pipeline)
                ++metal_live.pipelines;
              if (!bundle->library || !bundle->function || !bundle->pipeline)
                return {};
              return std::static_pointer_cast<void>(bundle);
            } @catch (...) {
              return {};
            }
          }
        }));
  }

  bool initialize(bool) override { return device_ != nil; }
  void shutdown() noexcept override {
    pipeline_cache_.invalidate_device(
        DIGITOR_RENDERER_METAL,
        reinterpret_cast<std::uintptr_t>((__bridge void *)device_));
  }
  DigitorRendererInfo info() const noexcept override { return info_; }
  NativePipelineCacheCounters
  native_pipeline_cache_counters() const noexcept override {
    return pipeline_cache_.counters();
  }
  std::size_t native_pipeline_cache_size() const noexcept override {
    return pipeline_cache_.size();
  }
  void clear_native_pipeline_cache_for_test() noexcept override {
    pipeline_cache_.invalidate_device(
        DIGITOR_RENDERER_METAL,
        reinterpret_cast<std::uintptr_t>((__bridge void *)device_));
  }
  NativeResourceCounts native_resource_counts() const noexcept override {
    return metal_live.snapshot();
  }

  DigitorResult execute_process_primary_wheels_gpu(
      std::span<const Color> source, std::uint32_t width, std::uint32_t height,
      std::int64_t timestamp, const PrimaryWheelsParameters &parameters,
      ProcessedGpuFramePtr &out) noexcept override {
    out.reset();
    QualificationScope qualification(*this, "primary-wheels/cpu-source");
    LocalCounts local;
    if (!width || !height || source.size() != std::size_t(width) * height)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    @autoreleasepool {
      @try {
        static NSString *code =
            @"#include <metal_stdlib>\nusing namespace metal;struct P{float4 "
            @"lift,gamma,gain,offset;uint4 enabled;uint count,w,h,pad;};float "
            @"sp(float x,float e){return "
            @"!isfinite(x)?x:(x<0?-pow(-x,e):pow(x,e));}kernel void "
            @"wheels(texture2d<float,access::read>i[[texture(0)]],texture2d<"
            @"float,access::write>o[[texture(1)]],constant "
            @"P&p[[buffer(0)]],uint2 "
            @"q[[thread_position_in_grid]]){if(q.x>=p.w||q.y>=p.h)return;"
            @"float4 "
            @"c=i.read(q);float "
            @"a=c.a;if(p.enabled.x)c.rgb+=p.lift.rgb+p.lift.a;if(p.enabled.y)c."
            @"rgb=float3(sp(c.r,1/(p.gamma.r*p.gamma.a)),sp(c.g,1/"
            @"(p.gamma.g*p.gamma.a)),sp(c.b,1/"
            @"(p.gamma.b*p.gamma.a)));if(p.enabled.z)c.rgb*=p.gain.rgb*p.gain."
            @"a;"
            @"if(p.enabled.w)c.rgb+=p.offset.rgb+p.offset.a;c.a=a;o.write(c,q);"
            @"}";
        auto cached = color_pipeline(false, code);
        id<MTLComputePipelineState> pipe = cached ? cached->pipeline : nil;
        MTLTextureDescriptor *d = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                         width:width
                                        height:height
                                     mipmapped:NO];
        d.storageMode = MTLStorageModeShared;
        d.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> input =
            make_texture(GpuFailurePoint::SourceResourceCreation,
                         "newTextureWithDescriptor(CPU source)", d, local);
        d.storageMode = MTLStorageModePrivate;
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        id<MTLTexture> output =
            make_texture(GpuFailurePoint::OutputResourceCreation,
                         "newTextureWithDescriptor(output)", d, local);
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        id<MTLTexture> preview =
            make_texture(GpuFailurePoint::PreviewDestinationCreation,
                         "newTextureWithDescriptor(preview)", d, local);
        id<MTLCommandQueue> queue =
            make_queue("newCommandQueue(compute)", local);
        if (!pipe || !input || !output || !preview || !queue)
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        if (fail(GpuFailurePoint::SourceUpload,
                 "replaceRegion(CPU source upload)"))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        [input replaceRegion:MTLRegionMake2D(0, 0, width, height)
                 mipmapLevel:0
                   withBytes:source.data()
                 bytesPerRow:width * sizeof(Color)];
        struct P {
          float lift[4], gamma[4], gain[4], offset[4];
          uint32_t enabled[4], count, w, h, pad;
        } p{};
        const auto &x = parameters.values();
        p = {{x.lift.r, x.lift.g, x.lift.b, x.lift_master},
             {x.gamma.r, x.gamma.g, x.gamma.b, x.gamma_master},
             {x.gain.r, x.gain.g, x.gain.b, x.gain_master},
             {x.offset.r, x.offset.g, x.offset.b, x.offset_master},
             {x.lift_enabled ? 1u : 0u, x.gamma_enabled ? 1u : 0u,
              x.gain_enabled ? 1u : 0u, x.offset_enabled ? 1u : 0u},
             uint32_t(source.size()),
             width,
             height,
             0};
        static_assert(sizeof(P) == 96);
        id<MTLBuffer> pb = make_buffer_bytes(
            GpuFailurePoint::ParameterResourceCreation,
            "newBufferWithBytes(primary parameters)", &p, sizeof(p), local);
        if (!pb)
          return DIGITOR_RESULT_OUT_OF_MEMORY;
        id<MTLCommandBuffer> command =
            make_command(queue, "commandBuffer(compute)", local);
        id<MTLComputeCommandEncoder> e = make_compute_encoder(command, local);
        MetalEncoderGuard e_guard(e);
        if (!command || !e)
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setComputePipelineState:pipe];
        if (fail(GpuFailurePoint::SourceTextureBinding, "setTexture(source)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setTexture:input atIndex:0];
        if (fail(GpuFailurePoint::OutputTextureBinding, "setTexture(output)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setTexture:output atIndex:1];
        if (fail(GpuFailurePoint::BufferBinding, "setBuffer(parameters)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setBuffer:pb offset:0 atIndex:0];
        NSUInteger n =
            std::min<NSUInteger>(pipe.maxTotalThreadsPerThreadgroup, 64);
        if (fail(GpuFailurePoint::DispatchSetup, "threadgroup/dispatch setup"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        if (fail(GpuFailurePoint::DispatchOrDraw, "dispatchThreads"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e dispatchThreads:MTLSizeMake(width, height, 1)
            threadsPerThreadgroup:MTLSizeMake(n, 1, 1)];
        if (!end_encoder(e_guard, "compute encoder endEncoding") ||
            !commit_and_wait(command))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        auto owner = std::shared_ptr<void>(
            new MetalPreviewOwner{output, preview, queue},
            [](void *v) { delete static_cast<MetalPreviewOwner *>(v); });
        auto typed_owner = std::static_pointer_cast<MetalPreviewOwner>(owner);
        typed_owner->tracked = true;
        ++metal_live.owners;
        local.transfer(*typed_owner);
        static std::atomic_uint64_t ids{100000};
        if (fail(GpuFailurePoint::ProcessedFrameCreation,
                 "ProcessedGpuFrame construction"))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        out = std::make_shared<ProcessedGpuFrame>(
            this, DIGITOR_RENDERER_METAL,
            GpuFrameMetadata{width, height, DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
                             GpuFrameAlpha::straight, timestamp, "linear-rgba"},
            ids++, owner, std::make_shared<std::atomic_bool>(true), true);
        provenance_.primary_wheels_enabled = true;
        provenance_.primary_wheels_parameter_identity = parameters.identity();
        provenance_.primary_wheels_shader_identity = "primary-wheels-msl-v1";
        provenance_.primary_wheels_pipeline_identity =
            "MTLComputePipelineState:primary-wheels-v1";
        provenance_.primary_wheels_source_bound =
            provenance_.primary_wheels_destination_bound =
                provenance_.primary_wheels_parameters_bound =
                    provenance_.command_recorded =
                        provenance_.dispatch_or_draw_issued =
                            provenance_.queue_submission_issued =
                                provenance_.synchronization_waited =
                                    provenance_.output_written = true;
        return DIGITOR_RESULT_OK;
      } @catch (...) {
        out.reset();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
    }
  }
  DigitorResult execute_validation_readback_primary_wheels(
      const ProcessedGpuFramePtr &frame,
      std::span<Color> out) noexcept override {
    QualificationScope qualification(*this, "validation-readback");
    LocalCounts local;
    @try {
      if (!frame || frame->backend() != DIGITOR_RENDERER_METAL ||
          out.size() !=
              std::size_t(frame->metadata().width) * frame->metadata().height)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      auto owner =
          std::static_pointer_cast<MetalPreviewOwner>(native_owner(*frame));
      if (!owner)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      const auto &m = frame->metadata();
      id<MTLBuffer> buffer = make_buffer_length(
          GpuFailurePoint::ValidationReadbackResourceCreation,
          "newBufferWithLength(validation)", out.size_bytes(), local);
      id<MTLCommandBuffer> command =
          make_command(owner->queue, "commandBuffer(validation)", local);
      id<MTLBlitCommandEncoder> blit =
          make_blit_encoder(command, "blitCommandEncoder(validation)", local);
      MetalEncoderGuard blit_guard(blit);
      if (!buffer || !command || !blit)
        return abort_encoder(blit_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
      if (fail(GpuFailurePoint::ValidationReadbackCopy,
               "copyFromTexture:toBuffer(validation)"))
        return abort_encoder(blit_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
      [blit copyFromTexture:owner->output
                       sourceSlice:0
                       sourceLevel:0
                      sourceOrigin:MTLOriginMake(0, 0, 0)
                        sourceSize:MTLSizeMake(m.width, m.height, 1)
                          toBuffer:buffer
                 destinationOffset:0
            destinationBytesPerRow:m.width * sizeof(Color)
          destinationBytesPerImage:out.size_bytes()];
      if (!end_encoder(blit_guard, "validation blit endEncoding") ||
          !commit_and_wait(command))
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      if (fail(GpuFailurePoint::ValidationCpuCopy,
               "CPU copy from validation MTLBuffer contents"))
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      std::memcpy(out.data(), buffer.contents, out.size_bytes());
      return DIGITOR_RESULT_OK;
    } @catch (...) {
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
  }

  DigitorResult execute_process_primary_wheels_gpu(
      const GpuSourceResource &source, std::int64_t timestamp,
      const PrimaryWheelsParameters &parameters,
      ProcessedGpuFramePtr &out) noexcept override {
    out.reset();
    QualificationScope qualification(*this, "primary-wheels/gpu-source");
    LocalCounts local;
    auto prior = std::static_pointer_cast<MetalPreviewOwner>(
        native_owner(*source.frame));
    if (!prior || prior->output == nil)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    @autoreleasepool {
      @try {
        static NSString *code =
            @"#include <metal_stdlib>\nusing namespace metal;struct P{float4 "
            @"lift,gamma,gain,offset;uint4 enabled;uint count,w,h,pad;};float "
            @"sp(float x,float e){return "
            @"!isfinite(x)?x:(x<0?-pow(-x,e):pow(x,e));}kernel void "
            @"wheels(texture2d<float,access::read>i[[texture(0)]],texture2d<"
            @"float,access::write>o[[texture(1)]],constant "
            @"P&p[[buffer(0)]],uint2 "
            @"q[[thread_position_in_grid]]){if(q.x>=p.w||q.y>=p.h)return;"
            @"float4 "
            @"c=i.read(q);float "
            @"a=c.a;if(p.enabled.x)c.rgb+=p.lift.rgb+p.lift.a;if(p.enabled.y)c."
            @"rgb=float3(sp(c.r,1/(p.gamma.r*p.gamma.a)),sp(c.g,1/"
            @"(p.gamma.g*p.gamma.a)),sp(c.b,1/"
            @"(p.gamma.b*p.gamma.a)));if(p.enabled.z)c.rgb*=p.gain.rgb*p.gain."
            @"a;"
            @"if(p.enabled.w)c.rgb+=p.offset.rgb+p.offset.a;c.a=a;o.write(c,q);"
            @"}";
        auto cached = color_pipeline(false, code);
        id<MTLComputePipelineState> pipe = cached ? cached->pipeline : nil;
        MTLTextureDescriptor *d = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                         width:source.width
                                        height:source.height
                                     mipmapped:NO];
        d.storageMode = MTLStorageModePrivate;
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        id<MTLTexture> output =
            make_texture(GpuFailurePoint::OutputResourceCreation,
                         "newTextureWithDescriptor(output)", d, local);
        id<MTLTexture> preview =
            make_texture(GpuFailurePoint::PreviewDestinationCreation,
                         "newTextureWithDescriptor(preview)", d, local);
        id<MTLCommandQueue> queue =
            make_queue("newCommandQueue(compute)", local);
        if (!pipe || !output || !preview || !queue)
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        struct P {
          float lift[4], gamma[4], gain[4], offset[4];
          uint32_t enabled[4], count, w, h, pad;
        } p{};
        const auto &x = parameters.values();
        p = {{x.lift.r, x.lift.g, x.lift.b, x.lift_master},
             {x.gamma.r, x.gamma.g, x.gamma.b, x.gamma_master},
             {x.gain.r, x.gain.g, x.gain.b, x.gain_master},
             {x.offset.r, x.offset.g, x.offset.b, x.offset_master},
             {x.lift_enabled ? 1u : 0u, x.gamma_enabled ? 1u : 0u,
              x.gain_enabled ? 1u : 0u, x.offset_enabled ? 1u : 0u},
             source.width * source.height,
             source.width,
             source.height,
             0};
        id<MTLBuffer> pb = make_buffer_bytes(
            GpuFailurePoint::ParameterResourceCreation,
            "newBufferWithBytes(primary parameters)", &p, sizeof(p), local);
        id<MTLCommandBuffer> command =
            make_command(queue, "commandBuffer(compute)", local);
        id<MTLComputeCommandEncoder> e = make_compute_encoder(command, local);
        MetalEncoderGuard e_guard(e);
        if (!pb || !command || !e)
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setComputePipelineState:pipe];
        if (fail(GpuFailurePoint::SourceTextureBinding,
                 "setTexture(GPU intermediate source)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setTexture:prior->output atIndex:0];
        if (fail(GpuFailurePoint::OutputTextureBinding, "setTexture(output)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setTexture:output atIndex:1];
        if (fail(GpuFailurePoint::BufferBinding, "setBuffer(parameters)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setBuffer:pb offset:0 atIndex:0];
        NSUInteger n =
            std::min<NSUInteger>(pipe.maxTotalThreadsPerThreadgroup, 64);
        if (fail(GpuFailurePoint::DispatchSetup,
                 "threadgroup/dispatch setup") ||
            fail(GpuFailurePoint::DispatchOrDraw, "dispatchThreads"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e dispatchThreads:MTLSizeMake(source.width, source.height, 1)
            threadsPerThreadgroup:MTLSizeMake(n, 1, 1)];
        if (!end_encoder(e_guard, "compute encoder endEncoding") ||
            !commit_and_wait(command))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        auto owner = std::shared_ptr<void>(
            new MetalPreviewOwner{output, preview, queue, prior},
            [](void *v) { delete static_cast<MetalPreviewOwner *>(v); });
        auto typed_owner = std::static_pointer_cast<MetalPreviewOwner>(owner);
        typed_owner->tracked = true;
        ++metal_live.owners;
        local.transfer(*typed_owner);
        static std::atomic_uint64_t ids{200000};
        if (fail(GpuFailurePoint::ProcessedFrameCreation,
                 "ProcessedGpuFrame construction"))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        out = std::make_shared<ProcessedGpuFrame>(
            this, DIGITOR_RENDERER_METAL,
            GpuFrameMetadata{source.width, source.height, source.format,
                             GpuFrameAlpha::straight, timestamp,
                             source.color_metadata_identity},
            ids++, owner, std::make_shared<std::atomic_bool>(true), true);
        provenance_.primary_wheels_source_bound =
            provenance_.primary_wheels_destination_bound =
                provenance_.primary_wheels_parameters_bound =
                    provenance_.command_recorded =
                        provenance_.dispatch_or_draw_issued =
                            provenance_.queue_submission_issued =
                                provenance_.synchronization_waited =
                                    provenance_.output_written = true;
        return DIGITOR_RESULT_OK;
      } @catch (...) {
        out.reset();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
    }
  }

  DigitorResult execute_process_log_wheels_gpu(
      std::span<const Color> source, std::uint32_t width, std::uint32_t height,
      std::int64_t timestamp, const LogWheelsParameters &parameters,
      ProcessedGpuFramePtr &out) noexcept override {
    out.reset();
    QualificationScope qualification(*this, "log-wheels/cpu-source");
    LocalCounts local;
    if (!width || !height || source.size() != std::size_t(width) * height)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    @autoreleasepool {
      @try {
        static NSString *code =
            @"#include <metal_stdlib>\nusing namespace metal;struct P{float4 shadows,midtones,highlights,globalWheel;uint4 enabled;float4 tonal;uint count,w,h,pad;};float sb(float a,float b,float x){float t=clamp((x-a)/(b-a),0.0f,1.0f);return t*t*(3.0f-2.0f*t);}kernel void log_wheels(texture2d<float,access::read>i[[texture(0)]],texture2d<float,access::write>o[[texture(1)]],constant P&p[[buffer(0)]],uint2 q[[thread_position_in_grid]]){if(q.x>=p.w||q.y>=p.h)return;float4 c=i.read(q);float a=c.a;float y=dot(c.rgb,float3(0.2126f,0.7152f,0.0722f));float hwid=p.tonal.z*0.5f;float sw=1.0f-sb(p.tonal.x-hwid,p.tonal.x+hwid,y);float hw=sb(p.tonal.y-hwid,p.tonal.y+hwid,y);float mw=max(0.0f,1.0f-sw-hw);float stop=(p.enabled.x?p.shadows.a*sw:0.0f)+(p.enabled.y?p.midtones.a*mw:0.0f)+(p.enabled.z?p.highlights.a*hw:0.0f)+(p.enabled.w?p.globalWheel.a:0.0f);float3 bal=(p.enabled.x?p.shadows.rgb*sw:float3(0.0f))+(p.enabled.y?p.midtones.rgb*mw:float3(0.0f))+(p.enabled.z?p.highlights.rgb*hw:float3(0.0f))+(p.enabled.w?p.globalWheel.rgb:float3(0.0f));c.rgb=c.rgb*exp2(stop)+bal;c.a=a;o.write(c,q);}";
        auto cached = color_pipeline(2, code);
        id<MTLComputePipelineState> pipe = cached ? cached->pipeline : nil;
        MTLTextureDescriptor *d = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                         width:width
                                        height:height
                                     mipmapped:NO];
        d.storageMode = MTLStorageModeShared;
        d.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> input =
            make_texture(GpuFailurePoint::SourceResourceCreation,
                         "newTextureWithDescriptor(CPU source)", d, local);
        d.storageMode = MTLStorageModePrivate;
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        id<MTLTexture> output =
            make_texture(GpuFailurePoint::OutputResourceCreation,
                         "newTextureWithDescriptor(output)", d, local);
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        id<MTLTexture> preview =
            make_texture(GpuFailurePoint::PreviewDestinationCreation,
                         "newTextureWithDescriptor(preview)", d, local);
        id<MTLCommandQueue> queue =
            make_queue("newCommandQueue(compute)", local);
        if (!pipe || !input || !output || !preview || !queue)
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        if (fail(GpuFailurePoint::SourceUpload,
                 "replaceRegion(CPU source upload)"))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        [input replaceRegion:MTLRegionMake2D(0, 0, width, height)
                 mipmapLevel:0
                   withBytes:source.data()
                 bytesPerRow:width * sizeof(Color)];
        struct alignas(16) P {
          float shadows[4], midtones[4], highlights[4], globalWheel[4];
          uint32_t enabled[4]; float tonal[4];
          uint32_t count, w, h, pad;
        } p{};
        const auto &x = parameters.values();
        p = {{x.shadows.rgb.r,x.shadows.rgb.g,x.shadows.rgb.b,x.shadows.master},
             {x.midtones.rgb.r,x.midtones.rgb.g,x.midtones.rgb.b,x.midtones.master},
             {x.highlights.rgb.r,x.highlights.rgb.g,x.highlights.rgb.b,x.highlights.master},
             {x.global.rgb.r,x.global.rgb.g,x.global.rgb.b,x.global.master},
             {x.shadows.enabled?1u:0u,x.midtones.enabled?1u:0u,x.highlights.enabled?1u:0u,x.global.enabled?1u:0u},
             {x.shadow_pivot,x.highlight_pivot,x.transition_width,0.0f},
             uint32_t(source.size()),width,height,0};
        static_assert(sizeof(P) == 112);
        id<MTLBuffer> pb = make_buffer_bytes(
            GpuFailurePoint::ParameterResourceCreation,
            "newBufferWithBytes(log parameters)", &p, sizeof(p), local);
        if (!pb)
          return DIGITOR_RESULT_OUT_OF_MEMORY;
        id<MTLCommandBuffer> command =
            make_command(queue, "commandBuffer(compute)", local);
        id<MTLComputeCommandEncoder> e = make_compute_encoder(command, local);
        MetalEncoderGuard e_guard(e);
        if (!command || !e)
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setComputePipelineState:pipe];
        if (fail(GpuFailurePoint::SourceTextureBinding, "setTexture(source)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setTexture:input atIndex:0];
        if (fail(GpuFailurePoint::OutputTextureBinding, "setTexture(output)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setTexture:output atIndex:1];
        if (fail(GpuFailurePoint::BufferBinding, "setBuffer(parameters)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setBuffer:pb offset:0 atIndex:0];
        NSUInteger n =
            std::min<NSUInteger>(pipe.maxTotalThreadsPerThreadgroup, 64);
        if (fail(GpuFailurePoint::DispatchSetup, "threadgroup/dispatch setup"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        if (fail(GpuFailurePoint::DispatchOrDraw, "dispatchThreads"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e dispatchThreads:MTLSizeMake(width, height, 1)
            threadsPerThreadgroup:MTLSizeMake(n, 1, 1)];
        if (!end_encoder(e_guard, "compute encoder endEncoding") ||
            !commit_and_wait(command))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        auto owner = std::shared_ptr<void>(
            new MetalPreviewOwner{output, preview, queue},
            [](void *v) { delete static_cast<MetalPreviewOwner *>(v); });
        auto typed_owner = std::static_pointer_cast<MetalPreviewOwner>(owner);
        typed_owner->tracked = true;
        ++metal_live.owners;
        local.transfer(*typed_owner);
        static std::atomic_uint64_t ids{100000};
        if (fail(GpuFailurePoint::ProcessedFrameCreation,
                 "ProcessedGpuFrame construction"))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        out = std::make_shared<ProcessedGpuFrame>(
            this, DIGITOR_RENDERER_METAL,
            GpuFrameMetadata{width, height, DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
                             GpuFrameAlpha::straight, timestamp, "linear-rgba"},
            ids++, owner, std::make_shared<std::atomic_bool>(true), true);
        provenance_.log_wheels_enabled = true;
        provenance_.log_wheels_parameter_identity = parameters.identity();
        provenance_.log_wheels_shader_identity = "log-wheels-msl-v1";
        provenance_.log_wheels_pipeline_identity =
            "MTLComputePipelineState:log-wheels-v1";
        provenance_.log_wheels_source_bound =
            provenance_.log_wheels_destination_bound =
                provenance_.log_wheels_parameters_bound =
                    provenance_.command_recorded =
                        provenance_.dispatch_or_draw_issued =
                            provenance_.queue_submission_issued =
                                provenance_.synchronization_waited =
                                    provenance_.output_written = true;
        return DIGITOR_RESULT_OK;
      } @catch (...) {
        out.reset();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
    }
  }

  DigitorResult execute_validation_readback_log_wheels(
      const ProcessedGpuFramePtr &frame,
      std::span<Color> out) noexcept override {
    QualificationScope qualification(*this, "validation-readback");
    LocalCounts local;
    @try {
      if (!frame || frame->backend() != DIGITOR_RENDERER_METAL ||
          out.size() !=
              std::size_t(frame->metadata().width) * frame->metadata().height)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      auto owner =
          std::static_pointer_cast<MetalPreviewOwner>(native_owner(*frame));
      if (!owner)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      const auto &m = frame->metadata();
      id<MTLBuffer> buffer = make_buffer_length(
          GpuFailurePoint::ValidationReadbackResourceCreation,
          "newBufferWithLength(validation)", out.size_bytes(), local);
      id<MTLCommandBuffer> command =
          make_command(owner->queue, "commandBuffer(validation)", local);
      id<MTLBlitCommandEncoder> blit =
          make_blit_encoder(command, "blitCommandEncoder(validation)", local);
      MetalEncoderGuard blit_guard(blit);
      if (!buffer || !command || !blit)
        return abort_encoder(blit_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
      if (fail(GpuFailurePoint::ValidationReadbackCopy,
               "copyFromTexture:toBuffer(validation)"))
        return abort_encoder(blit_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
      [blit copyFromTexture:owner->output
                       sourceSlice:0
                       sourceLevel:0
                      sourceOrigin:MTLOriginMake(0, 0, 0)
                        sourceSize:MTLSizeMake(m.width, m.height, 1)
                          toBuffer:buffer
                 destinationOffset:0
            destinationBytesPerRow:m.width * sizeof(Color)
          destinationBytesPerImage:out.size_bytes()];
      if (!end_encoder(blit_guard, "validation blit endEncoding") ||
          !commit_and_wait(command))
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      if (fail(GpuFailurePoint::ValidationCpuCopy,
               "CPU copy from validation MTLBuffer contents"))
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      std::memcpy(out.data(), buffer.contents, out.size_bytes());
      return DIGITOR_RESULT_OK;
    } @catch (...) {
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
  }

  DigitorResult execute_process_log_wheels_gpu(
      const GpuSourceResource &source, std::int64_t timestamp,
      const LogWheelsParameters &parameters,
      ProcessedGpuFramePtr &out) noexcept override {
    out.reset();
    QualificationScope qualification(*this, "log-wheels/gpu-source");
    LocalCounts local;
    auto prior = std::static_pointer_cast<MetalPreviewOwner>(
        native_owner(*source.frame));
    if (!prior || prior->output == nil)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    @autoreleasepool {
      @try {
        static NSString *code =
            @"#include <metal_stdlib>\nusing namespace metal;struct P{float4 shadows,midtones,highlights,globalWheel;uint4 enabled;float4 tonal;uint count,w,h,pad;};float sb(float a,float b,float x){float t=clamp((x-a)/(b-a),0.0f,1.0f);return t*t*(3.0f-2.0f*t);}kernel void log_wheels(texture2d<float,access::read>i[[texture(0)]],texture2d<float,access::write>o[[texture(1)]],constant P&p[[buffer(0)]],uint2 q[[thread_position_in_grid]]){if(q.x>=p.w||q.y>=p.h)return;float4 c=i.read(q);float a=c.a;float y=dot(c.rgb,float3(0.2126f,0.7152f,0.0722f));float hwid=p.tonal.z*0.5f;float sw=1.0f-sb(p.tonal.x-hwid,p.tonal.x+hwid,y);float hw=sb(p.tonal.y-hwid,p.tonal.y+hwid,y);float mw=max(0.0f,1.0f-sw-hw);float stop=(p.enabled.x?p.shadows.a*sw:0.0f)+(p.enabled.y?p.midtones.a*mw:0.0f)+(p.enabled.z?p.highlights.a*hw:0.0f)+(p.enabled.w?p.globalWheel.a:0.0f);float3 bal=(p.enabled.x?p.shadows.rgb*sw:float3(0.0f))+(p.enabled.y?p.midtones.rgb*mw:float3(0.0f))+(p.enabled.z?p.highlights.rgb*hw:float3(0.0f))+(p.enabled.w?p.globalWheel.rgb:float3(0.0f));c.rgb=c.rgb*exp2(stop)+bal;c.a=a;o.write(c,q);}";
        auto cached = color_pipeline(2, code);
        id<MTLComputePipelineState> pipe = cached ? cached->pipeline : nil;
        MTLTextureDescriptor *d = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                         width:source.width
                                        height:source.height
                                     mipmapped:NO];
        d.storageMode = MTLStorageModePrivate;
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        id<MTLTexture> output =
            make_texture(GpuFailurePoint::OutputResourceCreation,
                         "newTextureWithDescriptor(output)", d, local);
        id<MTLTexture> preview =
            make_texture(GpuFailurePoint::PreviewDestinationCreation,
                         "newTextureWithDescriptor(preview)", d, local);
        id<MTLCommandQueue> queue =
            make_queue("newCommandQueue(compute)", local);
        if (!pipe || !output || !preview || !queue)
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        struct alignas(16) P {
          float shadows[4], midtones[4], highlights[4], globalWheel[4];
          uint32_t enabled[4]; float tonal[4];
          uint32_t count, w, h, pad;
        } p{};
        const auto &x = parameters.values();
        p = {{x.shadows.rgb.r,x.shadows.rgb.g,x.shadows.rgb.b,x.shadows.master},
             {x.midtones.rgb.r,x.midtones.rgb.g,x.midtones.rgb.b,x.midtones.master},
             {x.highlights.rgb.r,x.highlights.rgb.g,x.highlights.rgb.b,x.highlights.master},
             {x.global.rgb.r,x.global.rgb.g,x.global.rgb.b,x.global.master},
             {x.shadows.enabled?1u:0u,x.midtones.enabled?1u:0u,x.highlights.enabled?1u:0u,x.global.enabled?1u:0u},
             {x.shadow_pivot,x.highlight_pivot,x.transition_width,0.0f},
             source.width * source.height,source.width,source.height,0};
        static_assert(sizeof(P) == 112);
        id<MTLBuffer> pb = make_buffer_bytes(
            GpuFailurePoint::ParameterResourceCreation,
            "newBufferWithBytes(log parameters)", &p, sizeof(p), local);
        id<MTLCommandBuffer> command =
            make_command(queue, "commandBuffer(compute)", local);
        id<MTLComputeCommandEncoder> e = make_compute_encoder(command, local);
        MetalEncoderGuard e_guard(e);
        if (!pb || !command || !e)
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setComputePipelineState:pipe];
        if (fail(GpuFailurePoint::SourceTextureBinding,
                 "setTexture(GPU intermediate source)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setTexture:prior->output atIndex:0];
        if (fail(GpuFailurePoint::OutputTextureBinding, "setTexture(output)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setTexture:output atIndex:1];
        if (fail(GpuFailurePoint::BufferBinding, "setBuffer(parameters)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setBuffer:pb offset:0 atIndex:0];
        NSUInteger n =
            std::min<NSUInteger>(pipe.maxTotalThreadsPerThreadgroup, 64);
        if (fail(GpuFailurePoint::DispatchSetup,
                 "threadgroup/dispatch setup") ||
            fail(GpuFailurePoint::DispatchOrDraw, "dispatchThreads"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e dispatchThreads:MTLSizeMake(source.width, source.height, 1)
            threadsPerThreadgroup:MTLSizeMake(n, 1, 1)];
        if (!end_encoder(e_guard, "compute encoder endEncoding") ||
            !commit_and_wait(command))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        auto owner = std::shared_ptr<void>(
            new MetalPreviewOwner{output, preview, queue, prior},
            [](void *v) { delete static_cast<MetalPreviewOwner *>(v); });
        auto typed_owner = std::static_pointer_cast<MetalPreviewOwner>(owner);
        typed_owner->tracked = true;
        ++metal_live.owners;
        local.transfer(*typed_owner);
        static std::atomic_uint64_t ids{200000};
        if (fail(GpuFailurePoint::ProcessedFrameCreation,
                 "ProcessedGpuFrame construction"))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        out = std::make_shared<ProcessedGpuFrame>(
            this, DIGITOR_RENDERER_METAL,
            GpuFrameMetadata{source.width, source.height, source.format,
                             GpuFrameAlpha::straight, timestamp,
                             source.color_metadata_identity},
            ids++, owner, std::make_shared<std::atomic_bool>(true), true);
        provenance_.log_wheels_source_bound =
            provenance_.log_wheels_destination_bound =
                provenance_.log_wheels_parameters_bound =
                    provenance_.command_recorded =
                        provenance_.dispatch_or_draw_issued =
                            provenance_.queue_submission_issued =
                                provenance_.synchronization_waited =
                                    provenance_.output_written = true;
        return DIGITOR_RESULT_OK;
      } @catch (...) {
        out.reset();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
    }
  }


  DigitorResult execute_process_hsl_qualifier_gpu(
      std::span<const Color> source, std::uint32_t width, std::uint32_t height,
      std::int64_t timestamp, const HslQualifierParameters &parameters,
      ProcessedGpuFramePtr &out) noexcept override {
    out.reset(); QualificationScope qualification(*this,"hsl-qualifier/cpu-source"); LocalCounts local;
    if(!width||!height||source.size()!=std::size_t(width)*height)return DIGITOR_RESULT_INVALID_ARGUMENT;
    @autoreleasepool { @try {
      static NSString *code=@"#include <metal_stdlib>\nusing namespace metal;struct R{float low,high,soft,pad;};struct P{R h,s,l;float4 clean;uint w,hgt,count,flags;};float lw(float v,R r){if(v>=r.low&&v<=r.high)return 1.0f;if(r.soft>0.0f&&v<r.low&&v>r.low-r.soft)return(v-r.low+r.soft)/r.soft;if(r.soft>0.0f&&v>r.high&&v<r.high+r.soft)return(r.high+r.soft-v)/r.soft;return 0.0f;}float hw(float x,R r){if(r.low<=r.high)return lw(x,r);R a=r;a.high=1.0f;R b=r;b.low=0.0f;return max(lw(x,a),lw(x,b));}float3 hsl(float3 c){float hi=max(c.r,max(c.g,c.b)),lo=min(c.r,min(c.g,c.b)),d=hi-lo,l=(hi+lo)*0.5f,s=d==0.0f?0.0f:d/max(1e-8f,1.0f-abs(2.0f*l-1.0f)),h=0.0f;if(d!=0.0f){if(hi==c.r)h=fmod((c.g-c.b)/d,6.0f);else if(hi==c.g)h=(c.b-c.r)/d+2.0f;else h=(c.r-c.g)/d+4.0f;h/=6.0f;if(h<0.0f)h+=1.0f;}return float3(h,s,l);}kernel void hsl_qualifier(texture2d<float,access::read>i[[texture(0)]],texture2d<float,access::write>o[[texture(1)]],constant P&p[[buffer(0)]],uint2 q[[thread_position_in_grid]]){if(q.x>=p.w||q.y>=p.hgt)return;float3 x=hsl(i.read(q).rgb);float m=hw(x.x,p.h)*lw(x.y,p.s)*lw(x.z,p.l);if(m<=p.clean.x)m=0.0f;if(m>=1.0f-p.clean.y)m=1.0f;if((p.flags&1u)!=0)m=1.0f-m;o.write(float4(m,m,m,1.0f),q);}";
      auto cached=color_pipeline(3,code);id<MTLComputePipelineState>pipe=cached?cached->pipeline:nil;
      MTLTextureDescriptor*d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:width height:height mipmapped:NO];d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead;id<MTLTexture>input=make_texture(GpuFailurePoint::SourceResourceCreation,"newTexture HSL source",d,local);d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;id<MTLTexture>output=make_texture(GpuFailurePoint::OutputResourceCreation,"newTexture HSL output",d,local);d.usage=MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget;id<MTLTexture>preview=make_texture(GpuFailurePoint::PreviewDestinationCreation,"newTexture HSL preview",d,local);id<MTLCommandQueue>queue=make_queue("newCommandQueue HSL",local);if(!pipe||!input||!output||!preview||!queue)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;[input replaceRegion:MTLRegionMake2D(0,0,width,height) mipmapLevel:0 withBytes:source.data() bytesPerRow:width*sizeof(Color)];
      struct alignas(16) R{float low,high,soft,pad;};struct alignas(16) P{R h,s,l;float clean[4];uint32_t w,hgt,count,flags;}p{};const auto&x=parameters.values();p={{x.hue.low,x.hue.high,x.hue.softness,0},{x.saturation.low,x.saturation.high,x.saturation.softness,0},{x.luminance.low,x.luminance.high,x.luminance.softness,0},{x.clean_black,x.clean_white,x.denoise,x.blur},width,height,uint32_t(source.size()),x.invert?1u:0u};id<MTLBuffer>pb=make_buffer_bytes(GpuFailurePoint::ParameterResourceCreation,"newBuffer HSL parameters",&p,sizeof(p),local);id<MTLCommandBuffer>command=make_command(queue,"commandBuffer HSL",local);id<MTLComputeCommandEncoder>e=make_compute_encoder(command,local);MetalEncoderGuard guard(e);if(!pb||!command||!e)return abort_encoder(guard,DIGITOR_RESULT_BACKEND_UNAVAILABLE);[e setComputePipelineState:pipe];[e setTexture:input atIndex:0];[e setTexture:output atIndex:1];[e setBuffer:pb offset:0 atIndex:0];NSUInteger n=std::min<NSUInteger>(pipe.maxTotalThreadsPerThreadgroup,64);[e dispatchThreads:MTLSizeMake(width,height,1) threadsPerThreadgroup:MTLSizeMake(n,1,1)];if(!end_encoder(guard,"HSL endEncoding")||!commit_and_wait(command))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;auto owner=std::shared_ptr<void>(new MetalPreviewOwner{output,preview,queue},[](void*v){delete static_cast<MetalPreviewOwner*>(v);});auto typed=std::static_pointer_cast<MetalPreviewOwner>(owner);typed->tracked=true;++metal_live.owners;local.transfer(*typed);static std::atomic_uint64_t ids{500000};out=std::make_shared<ProcessedGpuFrame>(this,DIGITOR_RENDERER_METAL,GpuFrameMetadata{width,height,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,GpuFrameAlpha::straight,timestamp,"hsl-matte-linear"},ids++,owner,std::make_shared<std::atomic_bool>(true),true);provenance_.hsl_qualifier_enabled=true;provenance_.hsl_qualifier_parameter_identity=parameters.identity();provenance_.hsl_qualifier_shader_identity="hsl-qualifier-msl-v1";provenance_.hsl_qualifier_pipeline_identity="MTLComputePipelineState:hsl-qualifier-v1";provenance_.hsl_qualifier_source_bound=provenance_.hsl_qualifier_destination_bound=provenance_.hsl_qualifier_parameters_bound=provenance_.command_recorded=provenance_.dispatch_or_draw_issued=provenance_.queue_submission_issued=provenance_.synchronization_waited=provenance_.output_written=true;provenance_.normal_preview_readback_count=0;return DIGITOR_RESULT_OK;
    } @catch(...){out.reset();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;} }
  }
  DigitorResult execute_process_hsl_qualifier_gpu(const GpuSourceResource&source,std::int64_t timestamp,const HslQualifierParameters&parameters,ProcessedGpuFramePtr&out)noexcept override{
    out.reset();auto prior=std::static_pointer_cast<MetalPreviewOwner>(native_owner(*source.frame));if(!prior||prior->output==nil)return DIGITOR_RESULT_INVALID_ARGUMENT;
    @autoreleasepool{@try{
      static NSString *code=@"#include <metal_stdlib>\nusing namespace metal;struct R{float low,high,soft,pad;};struct P{R h,s,l;float4 clean;uint w,hgt,count,flags;};float lw(float v,R r){if(v>=r.low&&v<=r.high)return 1.;if(r.soft>0.&&v<r.low&&v>r.low-r.soft)return(v-r.low+r.soft)/r.soft;if(r.soft>0.&&v>r.high&&v<r.high+r.soft)return(r.high+r.soft-v)/r.soft;return 0.;}float hw(float x,R r){if(r.low<=r.high)return lw(x,r);R a=r;a.high=1.;R b=r;b.low=0.;return max(lw(x,a),lw(x,b));}float3 hsl(float3 c){float hi=max(c.r,max(c.g,c.b)),lo=min(c.r,min(c.g,c.b)),d=hi-lo,l=(hi+lo)*.5,s=d==0.?0.:d/max(1e-8,1.-abs(2.*l-1.)),h=0.;if(d!=0.){if(hi==c.r)h=fmod((c.g-c.b)/d,6.);else if(hi==c.g)h=(c.b-c.r)/d+2.;else h=(c.r-c.g)/d+4.;h/=6.;if(h<0.)h+=1.;}return float3(h,s,l);}kernel void hsl_qualifier(texture2d<float,access::read>i[[texture(0)]],texture2d<float,access::write>o[[texture(1)]],constant P&p[[buffer(0)]],uint2 q[[thread_position_in_grid]]){if(q.x>=p.w||q.y>=p.hgt)return;float3 x=hsl(i.read(q).rgb);float m=hw(x.x,p.h)*lw(x.y,p.s)*lw(x.z,p.l);if(m<=p.clean.x)m=0.;if(m>=1.-p.clean.y)m=1.;if((p.flags&1u)!=0)m=1.-m;o.write(float4(m,m,m,1.),q);}";auto cached=color_pipeline(3,code);id<MTLComputePipelineState>pipe=cached?cached->pipeline:nil;LocalCounts local;MTLTextureDescriptor*d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:source.width height:source.height mipmapped:NO];d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;id<MTLTexture>output=make_texture(GpuFailurePoint::OutputResourceCreation,"newTexture HSL GPU output",d,local);d.usage=MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget;id<MTLTexture>preview=make_texture(GpuFailurePoint::PreviewDestinationCreation,"newTexture HSL GPU preview",d,local);id<MTLCommandQueue>queue=make_queue("newCommandQueue HSL GPU",local);if(!pipe||!output||!preview||!queue)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;struct alignas(16) R{float low,high,soft,pad;};struct alignas(16) P{R h,s,l;float clean[4];uint32_t w,hgt,count,flags;}p{};const auto&x=parameters.values();p={{x.hue.low,x.hue.high,x.hue.softness,0},{x.saturation.low,x.saturation.high,x.saturation.softness,0},{x.luminance.low,x.luminance.high,x.luminance.softness,0},{x.clean_black,x.clean_white,x.denoise,x.blur},source.width,source.height,source.width*source.height,x.invert?1u:0u};id<MTLBuffer>pb=make_buffer_bytes(GpuFailurePoint::ParameterResourceCreation,"newBuffer HSL GPU parameters",&p,sizeof(p),local);id<MTLCommandBuffer>command=make_command(queue,"commandBuffer HSL GPU",local);id<MTLComputeCommandEncoder>e=make_compute_encoder(command,local);MetalEncoderGuard guard(e);if(!pb||!command||!e)return abort_encoder(guard,DIGITOR_RESULT_BACKEND_UNAVAILABLE);[e setComputePipelineState:pipe];[e setTexture:prior->output atIndex:0];[e setTexture:output atIndex:1];[e setBuffer:pb offset:0 atIndex:0];NSUInteger n=std::min<NSUInteger>(pipe.maxTotalThreadsPerThreadgroup,64);[e dispatchThreads:MTLSizeMake(source.width,source.height,1) threadsPerThreadgroup:MTLSizeMake(n,1,1)];if(!end_encoder(guard,"HSL GPU endEncoding")||!commit_and_wait(command))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;auto owner=std::shared_ptr<void>(new MetalPreviewOwner{output,preview,queue,prior},[](void*v){delete static_cast<MetalPreviewOwner*>(v);});auto typed=std::static_pointer_cast<MetalPreviewOwner>(owner);typed->tracked=true;++metal_live.owners;local.transfer(*typed);static std::atomic_uint64_t ids{510000};out=std::make_shared<ProcessedGpuFrame>(this,DIGITOR_RENDERER_METAL,GpuFrameMetadata{source.width,source.height,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,GpuFrameAlpha::straight,timestamp,"hsl-matte-linear"},ids++,owner,std::make_shared<std::atomic_bool>(true),true);provenance_.hsl_qualifier_enabled=true;provenance_.hsl_qualifier_parameter_identity=parameters.identity();provenance_.hsl_qualifier_shader_identity="hsl-qualifier-msl-v1";provenance_.hsl_qualifier_pipeline_identity="MTLComputePipelineState:hsl-qualifier-v1";provenance_.hsl_qualifier_source_bound=provenance_.hsl_qualifier_destination_bound=provenance_.hsl_qualifier_parameters_bound=provenance_.command_recorded=provenance_.dispatch_or_draw_issued=provenance_.queue_submission_issued=provenance_.synchronization_waited=provenance_.output_written=true;provenance_.normal_preview_readback_count=0;return DIGITOR_RESULT_OK;}@catch(...){out.reset();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}}
  }
  DigitorResult execute_validation_readback_hsl_qualifier(const ProcessedGpuFramePtr&frame,std::span<float>out)noexcept override{if(!frame||out.size()!=std::size_t(frame->metadata().width)*frame->metadata().height)return DIGITOR_RESULT_INVALID_ARGUMENT;std::vector<Color>rgba(out.size());auto r=execute_validation_readback_log_wheels(frame,rgba);if(r!=DIGITOR_RESULT_OK)return r;for(std::size_t i=0;i<out.size();++i)out[i]=rgba[i].r;return DIGITOR_RESULT_OK;}

  DigitorResult execute_process_curves_gpu(std::span<const Color> source, std::uint32_t width,
                             std::uint32_t height, std::int64_t timestamp,
                             const CompiledRgbCurves &compiled,
                             ProcessedGpuFramePtr &out) noexcept override {
    out.reset();
    QualificationScope qualification(*this, "rgb-curves/cpu-source");
    LocalCounts local;
    if (!width || !height || source.size() != std::size_t(width) * height)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    @autoreleasepool {
      @try {
        static NSString *code =
            @"#include <metal_stdlib>\nusing namespace metal;"
             "struct M{float lo,hi,first,last,sb,sa;uint "
             "extrap,enabled;};struct "
             "P{M m[4];uint size,w,h;};"
             "float cv(device const float*l,constant P&p,uint k,float x){M "
             "m=p.m[k];if(!m.enabled||!isfinite(x))return x;if(x<m.lo)return "
             "m.extrap==2?m.first+m.sb*(x-m.lo):m.first;if(x>m.hi)return "
             "m.extrap==2?m.last+m.sa*(x-m.hi):m.last;float "
             "u=(x-m.lo)/(m.hi-m.lo)*float(p.size-1);uint "
             "a=min(uint(u),p.size-1),b=min(a+1,p.size-1);return "
             "mix(l[k*p.size+a],l[k*p.size+b],u-float(a));}"
             "kernel void curves(texture2d<float,access::read> "
             "i[[texture(0)]],texture2d<float,access::write> "
             "o[[texture(1)]],device const float*l[[buffer(0)]],constant "
             "P&p[[buffer(1)]],uint2 "
             "q[[thread_position_in_grid]]){if(q.x>=p.w||q.y>=p.h)return;"
             "float4 "
             "c=i.read(q);float "
             "a=c.a;c.r=cv(l,p,0,c.r);c.g=cv(l,p,0,c.g);c.b=cv(l,p,0,c.b);c.r="
             "cv("
             "l,p,1,c.r);c.g=cv(l,p,2,c.g);c.b=cv(l,p,3,c.b);c.a=a;o.write(c,q)"
             ";"
             "}";
        auto cached = color_pipeline(1, code);
        id<MTLComputePipelineState> pipe = cached ? cached->pipeline : nil;
        MTLTextureDescriptor *sd = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                         width:width
                                        height:height
                                     mipmapped:NO];
        sd.storageMode = MTLStorageModeShared;
        sd.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> input =
            make_texture(GpuFailurePoint::SourceResourceCreation,
                         "newTextureWithDescriptor(CPU source)", sd, local);
        sd.storageMode = MTLStorageModePrivate;
        sd.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
        id<MTLTexture> output =
            make_texture(GpuFailurePoint::OutputResourceCreation,
                         "newTextureWithDescriptor(output)", sd, local);
        sd.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        id<MTLTexture> preview =
            make_texture(GpuFailurePoint::PreviewDestinationCreation,
                         "newTextureWithDescriptor(preview)", sd, local);
        id<MTLCommandQueue> queue =
            make_queue("newCommandQueue(compute)", local);
        if (!pipe || !input || !output || !preview || !queue)
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        if (fail(GpuFailurePoint::SourceUpload,
                 "replaceRegion(CPU source upload)"))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        [input replaceRegion:MTLRegionMake2D(0, 0, width, height)
                 mipmapLevel:0
                   withBytes:source.data()
                 bytesPerRow:width * sizeof(Color)];
        struct M {
          float lo, hi, first, last, sb, sa;
          uint32_t extrap, enabled;
        };
        struct P {
          M m[4];
          uint32_t size, w, h;
        } p{};
        std::vector<float> lut;
        lut.reserve(std::size_t(compiled.lut_size()) * 4);
        for (unsigned k = 0; k < 4; k++) {
          const auto &c = compiled.curves()[k];
          p.m[k] = {c.domain_min,
                    c.domain_max,
                    c.first_value,
                    c.last_value,
                    c.slope_before,
                    c.slope_after,
                    uint32_t(c.extrapolation),
                    c.enabled && !c.identity ? 1u : 0u};
          lut.insert(lut.end(), c.samples.begin(), c.samples.end());
        }
        p.size = compiled.lut_size();
        p.w = width;
        p.h = height;
        if (fail(GpuFailurePoint::LutUpload, "newBufferWithBytes(LUT upload)"))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        id<MTLBuffer> lb = make_buffer_bytes(
            GpuFailurePoint::LutResourceCreation, "newBufferWithBytes(LUT)",
            lut.data(), lut.size() * sizeof(float), local);
        if (!lb)
          return DIGITOR_RESULT_OUT_OF_MEMORY;
        id<MTLCommandBuffer> command =
            make_command(queue, "commandBuffer(compute)", local);
        id<MTLComputeCommandEncoder> e = make_compute_encoder(command, local);
        MetalEncoderGuard e_guard(e);
        if (!command || !e)
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setComputePipelineState:pipe];
        if (fail(GpuFailurePoint::SourceTextureBinding, "setTexture(source)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setTexture:input atIndex:0];
        if (fail(GpuFailurePoint::OutputTextureBinding, "setTexture(output)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setTexture:output atIndex:1];
        if (fail(GpuFailurePoint::BufferBinding, "setBuffer(LUT/parameters)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setBuffer:lb offset:0 atIndex:0];
        [e setBytes:&p length:sizeof(p) atIndex:1];
        NSUInteger g =
            std::min<NSUInteger>(pipe.maxTotalThreadsPerThreadgroup, 64);
        if (fail(GpuFailurePoint::DispatchSetup,
                 "threadgroup/dispatch setup") ||
            fail(GpuFailurePoint::DispatchOrDraw, "dispatchThreads"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e dispatchThreads:MTLSizeMake(width, height, 1)
            threadsPerThreadgroup:MTLSizeMake(g, 1, 1)];
        if (!end_encoder(e_guard, "compute encoder endEncoding") ||
            !commit_and_wait(command))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        auto owner = std::shared_ptr<void>(
            new MetalPreviewOwner{output, preview, queue},
            [](void *v) { delete static_cast<MetalPreviewOwner *>(v); });
        auto typed_owner = std::static_pointer_cast<MetalPreviewOwner>(owner);
        typed_owner->tracked = true;
        ++metal_live.owners;
        local.transfer(*typed_owner);
        auto ready = std::make_shared<std::atomic_bool>(true);
        static std::atomic_uint64_t ids{1};
        if (fail(GpuFailurePoint::ProcessedFrameCreation,
                 "ProcessedGpuFrame construction"))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        out = std::make_shared<ProcessedGpuFrame>(
            this, DIGITOR_RENDERER_METAL,
            GpuFrameMetadata{width, height, DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
                             GpuFrameAlpha::straight, timestamp, "linear-rgba"},
            ids++, owner, ready, true);
        provenance_.curve_source_bound = provenance_.curve_destination_bound =
            provenance_.curve_lut_bound = provenance_.curve_parameters_bound =
                true;
        provenance_.command_recorded = provenance_.dispatch_or_draw_issued =
            provenance_.queue_submission_issued =
                provenance_.synchronization_waited =
                    provenance_.output_written = true;
        provenance_.readback_performed = false;
        return DIGITOR_RESULT_OK;
      } @catch (...) {
        out.reset();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
    }
  }
  DigitorResult
  execute_process_curves_gpu(const GpuSourceResource &source,
                             std::int64_t timestamp,
                             const CompiledRgbCurves &compiled,
                             ProcessedGpuFramePtr &out) noexcept override {
    out.reset();
    QualificationScope qualification(*this, "rgb-curves/gpu-source");
    LocalCounts local;
    auto prior = std::static_pointer_cast<MetalPreviewOwner>(
        native_owner(*source.frame));
    if (!prior || prior->output == nil)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    @autoreleasepool {
      @try {
        static NSString *code =
            @"#include <metal_stdlib>\nusing namespace metal;struct M{float "
            @"lo,hi,first,last,sb,sa;uint extrap,enabled;};struct P{M "
            @"m[4];uint "
            @"size,w,h;};float cv(device const float*l,constant P&p,uint "
            @"k,float "
            @"x){M m=p.m[k];if(!m.enabled||!isfinite(x))return "
            @"x;if(x<m.lo)return "
            @"m.extrap==2?m.first+m.sb*(x-m.lo):m.first;if(x>m.hi)return "
            @"m.extrap==2?m.last+m.sa*(x-m.hi):m.last;float "
            @"u=(x-m.lo)/(m.hi-m.lo)*float(p.size-1);uint "
            @"a=min(uint(u),p.size-1),b=min(a+1,p.size-1);return "
            @"mix(l[k*p.size+a],l[k*p.size+b],u-float(a));}kernel void "
            @"curves(texture2d<float,access::read>i[[texture(0)]],texture2d<"
            @"float,access::write>o[[texture(1)]],device const "
            @"float*l[[buffer(0)]],constant P&p[[buffer(1)]],uint2 "
            @"q[[thread_position_in_grid]]){if(q.x>=p.w||q.y>=p.h)return;"
            @"float4 "
            @"c=i.read(q);float "
            @"a=c.a;c.r=cv(l,p,0,c.r);c.g=cv(l,p,0,c.g);c.b=cv(l,p,0,c.b);c.r="
            @"cv("
            @"l,p,1,c.r);c.g=cv(l,p,2,c.g);c.b=cv(l,p,3,c.b);c.a=a;o.write(c,q)"
            @";"
            @"}";
        auto cached = color_pipeline(1, code);
        id<MTLComputePipelineState> pipe = cached ? cached->pipeline : nil;
        MTLTextureDescriptor *d = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                         width:source.width
                                        height:source.height
                                     mipmapped:NO];
        d.storageMode = MTLStorageModePrivate;
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        id<MTLTexture> output =
            make_texture(GpuFailurePoint::OutputResourceCreation,
                         "newTextureWithDescriptor(output)", d, local);
        id<MTLTexture> preview =
            make_texture(GpuFailurePoint::PreviewDestinationCreation,
                         "newTextureWithDescriptor(preview)", d, local);
        id<MTLCommandQueue> queue =
            make_queue("newCommandQueue(compute)", local);
        if (!pipe || !output || !preview || !queue)
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        struct M {
          float lo, hi, first, last, sb, sa;
          uint32_t extrap, enabled;
        };
        struct P {
          M m[4];
          uint32_t size, w, h;
        } p{};
        std::vector<float> lut;
        for (unsigned k = 0; k < 4; k++) {
          const auto &c = compiled.curves()[k];
          p.m[k] = {c.domain_min,
                    c.domain_max,
                    c.first_value,
                    c.last_value,
                    c.slope_before,
                    c.slope_after,
                    uint32_t(c.extrapolation),
                    c.enabled && !c.identity ? 1u : 0u};
          lut.insert(lut.end(), c.samples.begin(), c.samples.end());
        }
        p.size = compiled.lut_size();
        p.w = source.width;
        p.h = source.height;
        if (fail(GpuFailurePoint::LutUpload, "newBufferWithBytes(LUT upload)"))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        id<MTLBuffer> lb = make_buffer_bytes(
            GpuFailurePoint::LutResourceCreation, "newBufferWithBytes(LUT)",
            lut.data(), lut.size() * sizeof(float), local);
        id<MTLCommandBuffer> command =
            make_command(queue, "commandBuffer(compute)", local);
        id<MTLComputeCommandEncoder> e = make_compute_encoder(command, local);
        MetalEncoderGuard e_guard(e);
        if (!lb || !command || !e)
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setComputePipelineState:pipe];
        if (fail(GpuFailurePoint::SourceTextureBinding,
                 "setTexture(GPU intermediate source)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setTexture:prior->output atIndex:0];
        if (fail(GpuFailurePoint::OutputTextureBinding, "setTexture(output)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setTexture:output atIndex:1];
        if (fail(GpuFailurePoint::BufferBinding, "setBuffer(LUT/parameters)"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e setBuffer:lb offset:0 atIndex:0];
        [e setBytes:&p length:sizeof(p) atIndex:1];
        NSUInteger n =
            std::min<NSUInteger>(pipe.maxTotalThreadsPerThreadgroup, 64);
        if (fail(GpuFailurePoint::DispatchSetup,
                 "threadgroup/dispatch setup") ||
            fail(GpuFailurePoint::DispatchOrDraw, "dispatchThreads"))
          return abort_encoder(e_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [e dispatchThreads:MTLSizeMake(source.width, source.height, 1)
            threadsPerThreadgroup:MTLSizeMake(n, 1, 1)];
        if (!end_encoder(e_guard, "compute encoder endEncoding") ||
            !commit_and_wait(command))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        auto owner = std::shared_ptr<void>(
            new MetalPreviewOwner{output, preview, queue, prior},
            [](void *v) { delete static_cast<MetalPreviewOwner *>(v); });
        auto typed_owner = std::static_pointer_cast<MetalPreviewOwner>(owner);
        typed_owner->tracked = true;
        ++metal_live.owners;
        local.transfer(*typed_owner);
        static std::atomic_uint64_t ids{300000};
        if (fail(GpuFailurePoint::ProcessedFrameCreation,
                 "ProcessedGpuFrame construction"))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        out = std::make_shared<ProcessedGpuFrame>(
            this, DIGITOR_RENDERER_METAL,
            GpuFrameMetadata{source.width, source.height, source.format,
                             GpuFrameAlpha::straight, timestamp,
                             source.color_metadata_identity},
            ids++, owner, std::make_shared<std::atomic_bool>(true), true);
        provenance_.curve_source_bound = provenance_.curve_destination_bound =
            provenance_.curve_lut_bound = provenance_.curve_parameters_bound =
                provenance_.command_recorded =
                    provenance_.dispatch_or_draw_issued =
                        provenance_.queue_submission_issued =
                            provenance_.synchronization_waited =
                                provenance_.output_written = true;
        provenance_.readback_performed = false;
        return DIGITOR_RESULT_OK;
      } @catch (...) {
        out.reset();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
    }
  }
  DigitorResult execute_create_preview_consumer(
      const ProcessedGpuFramePtr &frame,
      std::shared_ptr<PreviewConsumerDestination> &out) noexcept override {
    QualificationScope qualification(*this, "preview-consumer/create");
    LocalCounts local;
    @try {
      @autoreleasepool {
        out.reset();
        if (!frame || !device_)
          return DIGITOR_RESULT_NOT_INITIALIZED;
        if (fail(GpuFailurePoint::PreviewAcquisition,
                 "Metal consumer owner acquisition"))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        const auto &m = frame->metadata();
        MTLTextureDescriptor *d = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                         width:m.width
                                        height:m.height
                                     mipmapped:NO];
        d.storageMode = MTLStorageModePrivate;
        d.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture =
            make_texture(GpuFailurePoint::PreviewDestinationCreation,
                         "newTextureWithDescriptor(consumer)", d, local);
        id<MTLCommandQueue> queue =
            make_queue("newCommandQueue(consumer)", local);
        if (!texture || !queue)
          return DIGITOR_RESULT_OUT_OF_MEMORY;
        auto owner = std::shared_ptr<void>(
            new MetalConsumerOwner{device_, texture, queue},
            [](void *v) { delete static_cast<MetalConsumerOwner *>(v); });
        auto typed_owner = std::static_pointer_cast<MetalConsumerOwner>(owner);
        typed_owner->tracked = true;
        ++metal_live.consumers;
        --local.textures;
        --local.queues;
        static std::atomic_uint64_t tokens{1};
        out = std::make_shared<PreviewConsumerDestination>(
            PreviewConsumerMetadata{DIGITOR_RENDERER_METAL, this, m.width,
                                    m.height, m.format,
                                    GpuPrecisionMode::Float32},
            tokens++, owner, std::make_shared<std::atomic_bool>(true),
            [this](const ProcessedGpuFramePtr &f,
                   const std::shared_ptr<void> &d) {
              QualificationScope qualification(*this,
                                               "preview-consumer/submit");
              LocalCounts local;
              @try {
                @autoreleasepool {
                  auto source = std::static_pointer_cast<MetalPreviewOwner>(
                      native_owner(*f));
                  auto destination =
                      std::static_pointer_cast<MetalConsumerOwner>(d);
                  if (!source || !destination || destination->device != device_)
                    return DIGITOR_RESULT_INVALID_ARGUMENT;
                  id<MTLCommandBuffer> command =
                      make_command(destination->queue,
                                   "commandBuffer(consumer blit)", local);
                  id<MTLBlitCommandEncoder> blit = make_blit_encoder(
                      command, "blitCommandEncoder(consumer)", local);
                  MetalEncoderGuard blit_guard(blit);
                  if (!command || !blit)
                    return abort_encoder(blit_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
                  const auto &m = f->metadata();
                  if (fail(GpuFailurePoint::ResourceBinding,
                           "consumer blit source/destination binding") ||
                      fail(GpuFailurePoint::PreviewPresentation,
                           "consumer preview presentation copy") ||
                      fail(GpuFailurePoint::ConsumerCopySubmission,
                           "copyFromTexture:toTexture(consumer)"))
                    return abort_encoder(blit_guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
                  [blit copyFromTexture:source->output
                            sourceSlice:0
                            sourceLevel:0
                           sourceOrigin:MTLOriginMake(0, 0, 0)
                             sourceSize:MTLSizeMake(m.width, m.height, 1)
                              toTexture:destination->texture
                       destinationSlice:0
                       destinationLevel:0
                      destinationOrigin:MTLOriginMake(0, 0, 0)];
                  if (!end_encoder(blit_guard, "consumer blit endEncoding") ||
                      !commit_and_wait(command))
                    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
                  return DIGITOR_RESULT_OK;
                }
              } @catch (...) {
                return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
              }
            });
        return DIGITOR_RESULT_OK;
      }
    } @catch (...) {
      out.reset();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
  }
  DigitorResult execute_present_gpu_frame(
      const ProcessedGpuFramePtr &frame) noexcept override {
    QualificationScope qualification(*this, "preview-direct/submit");
    LocalCounts local;
    bool acquired = false;
    @try {
      if (!frame)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      if (fail(GpuFailurePoint::PreviewAcquisition,
               "ProcessedGpuFrame::acquire"))
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      if (frame->acquire(this, DIGITOR_RENDERER_METAL) != DIGITOR_RESULT_OK)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      acquired = true;
      provenance_.preview_acquisition_balance = 1;
      auto owner =
          std::static_pointer_cast<MetalPreviewOwner>(native_owner(*frame));
      if (!owner) {
        (void)frame->release(this);
        acquired = false;
        provenance_.preview_acquisition_balance = 0;
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      id<MTLCommandBuffer> c =
          make_command(owner->queue, "commandBuffer(direct preview)", local);
      id<MTLBlitCommandEncoder> b =
          make_blit_encoder(c, "blitCommandEncoder(direct preview)", local);
      MetalEncoderGuard b_guard(b);
      if (!c || !b) {
        (void)frame->release(this);
        acquired = false;
        provenance_.preview_acquisition_balance = 0;
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      auto m = frame->metadata();
      if (fail(GpuFailurePoint::PreviewPresentation,
               "copyFromTexture(direct preview)")) {
        (void)b_guard.finish(false);
        (void)frame->release(this);
        acquired = false;
        provenance_.preview_acquisition_balance = 0;
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      [b copyFromTexture:owner->output
                sourceSlice:0
                sourceLevel:0
               sourceOrigin:MTLOriginMake(0, 0, 0)
                 sourceSize:MTLSizeMake(m.width, m.height, 1)
                  toTexture:owner->preview
           destinationSlice:0
           destinationLevel:0
          destinationOrigin:MTLOriginMake(0, 0, 0)];
      auto r = end_encoder(b_guard, "direct preview blit endEncoding") &&
                       commit_and_wait(c)
                   ? DIGITOR_RESULT_OK
                   : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      (void)frame->release(this);
      acquired = false;
      provenance_.preview_acquisition_balance = 0;
      if (r == DIGITOR_RESULT_OK) {
        provenance_.preview_source = PreviewSource::gpu;
        provenance_.direct_preview_consumed = true;
        provenance_.readback_performed = false;
        provenance_.normal_preview_readback_count = 0;
      }
      return r;
    } @catch (...) {
      if (acquired) {
        (void)frame->release(this);
        provenance_.preview_acquisition_balance = 0;
      }
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
  }

  DigitorResult create_texture(const DigitorTextureDesc &description,
                               void **out) noexcept override {
    if (out == nullptr)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out = nullptr;
    @autoreleasepool {
      const MTLPixelFormat format = metal_format(description.format);
      if (device_ == nil || format == MTLPixelFormatInvalid ||
          description.width == 0 || description.height == 0)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:format
                                       width:description.width
                                      height:description.height
                                   mipmapped:NO];
      descriptor.storageMode = MTLStorageModePrivate;
      descriptor.usage = 0;
      if (description.usage & DIGITOR_TEXTURE_USAGE_SAMPLED)
        descriptor.usage |= MTLTextureUsageShaderRead;
      if (description.usage & DIGITOR_TEXTURE_USAGE_STORAGE)
        descriptor.usage |= MTLTextureUsageShaderWrite;
      if (description.usage & DIGITOR_TEXTURE_USAGE_RENDER_TARGET)
        descriptor.usage |= MTLTextureUsageRenderTarget;
      id<MTLTexture> texture = [device_ newTextureWithDescriptor:descriptor];
      if (texture == nil)
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      *out = (__bridge_retained void *)texture;
      return DIGITOR_RESULT_OK;
    }
  }

  DigitorResult create_buffer(const DigitorBufferDesc &description,
                              void **out) noexcept override {
    if (out == nullptr)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out = nullptr;
    @autoreleasepool {
      if (device_ == nil || description.size == 0)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      if constexpr (sizeof(NSUInteger) < sizeof(description.size)) {
        if (description.size > std::numeric_limits<NSUInteger>::max()) {
          return DIGITOR_RESULT_INVALID_ARGUMENT;
        }
      }
      const bool host_visible =
          (description.usage &
           (DIGITOR_BUFFER_USAGE_UPLOAD | DIGITOR_BUFFER_USAGE_STAGING)) != 0;
      const MTLResourceOptions options = host_visible
                                             ? MTLResourceStorageModeShared
                                             : MTLResourceStorageModePrivate;
      id<MTLBuffer> buffer =
          [device_ newBufferWithLength:static_cast<NSUInteger>(description.size)
                               options:options];
      if (buffer == nil)
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      *out = (__bridge_retained void *)buffer;
      return DIGITOR_RESULT_OK;
    }
  }

  DigitorResult create_sampler(const DigitorSamplerDesc &description,
                               void **out) noexcept override {
    if (out == nullptr)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out = nullptr;
    @autoreleasepool {
      if (device_ == nil)
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      MTLSamplerDescriptor *descriptor = [[MTLSamplerDescriptor alloc] init];
      descriptor.minFilter = description.min_filter == DIGITOR_FILTER_LINEAR
                                 ? MTLSamplerMinMagFilterLinear
                                 : MTLSamplerMinMagFilterNearest;
      descriptor.magFilter = description.mag_filter == DIGITOR_FILTER_LINEAR
                                 ? MTLSamplerMinMagFilterLinear
                                 : MTLSamplerMinMagFilterNearest;
      descriptor.normalizedCoordinates =
          description.normalized_coordinates != 0;
      id<MTLSamplerState> sampler =
          [device_ newSamplerStateWithDescriptor:descriptor];
      if (sampler == nil)
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      *out = (__bridge_retained void *)sampler;
      return DIGITOR_RESULT_OK;
    }
  }

  DigitorResult map_buffer(void *object, uint64_t offset, uint64_t,
                           void **out) noexcept override {
    if (out == nullptr)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out = nullptr;
    if (object == nullptr)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)object;
    if (offset > buffer.length)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    void *contents = buffer.contents;
    if (contents == nullptr)
      return DIGITOR_RESULT_UNSUPPORTED;
    *out = static_cast<uint8_t *>(contents) + static_cast<std::size_t>(offset);
    return DIGITOR_RESULT_OK;
  }

  void unmap_buffer(void *) noexcept override {}

  DigitorResult render_rgba8(uint32_t width, uint32_t height,
                             std::span<const uint8_t> source,
                             std::vector<uint8_t> &out) noexcept override {
    @autoreleasepool {
      if (width == 0 || height == 0) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      std::size_t byte_count = width;
      if (byte_count > std::numeric_limits<std::size_t>::max() / height) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      byte_count *= height;
      if (byte_count > std::numeric_limits<std::size_t>::max() / 4) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      byte_count *= 4;
      if (!source.empty() && source.size() != byte_count)
        return DIGITOR_RESULT_INVALID_ARGUMENT;

      MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                       width:width
                                      height:height
                                   mipmapped:NO];
      descriptor.usage =
          MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
      descriptor.storageMode = MTLStorageModeShared;
      id<MTLTexture> target = [device_ newTextureWithDescriptor:descriptor];
      id<MTLCommandQueue> queue = [device_ newCommandQueue];
      if (target == nil || queue == nil)
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      id<MTLCommandBuffer> command_buffer = [queue commandBuffer];
      if (command_buffer == nil)
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

      if (source.empty()) {
        MTLRenderPassDescriptor *pass =
            [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
        id<MTLRenderCommandEncoder> encoder =
            [command_buffer renderCommandEncoderWithDescriptor:pass];
        if (encoder == nil)
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        [encoder endEncoding];
      } else {
        [target replaceRegion:MTLRegionMake2D(0, 0, width, height)
                  mipmapLevel:0
                    withBytes:source.data()
                  bytesPerRow:static_cast<NSUInteger>(width) * 4];
      }
      [command_buffer commit];
      [command_buffer waitUntilCompleted];
      if (command_buffer.status != MTLCommandBufferStatusCompleted) {
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      try {
        out.resize(byte_count);
      } catch (...) {
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      }
      [target getBytes:out.data()
           bytesPerRow:static_cast<NSUInteger>(width) * 4
            fromRegion:MTLRegionMake2D(0, 0, width, height)
           mipmapLevel:0];
      return DIGITOR_RESULT_OK;
    }
  }

  DigitorResult grade_rgba32f(std::span<const Color> source,
                              std::span<Color> out,
                              const ColorGrade &p) noexcept override {
    begin_grade_provenance(DIGITOR_RENDERER_METAL, true, info_.device_name,
                           "Metal runtime compiler", "grade-msl-v1:grade",
                           "MTLComputePipelineState:grade-v1");
    if (source.size() != out.size())
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (source.empty())
      return DIGITOR_RESULT_OK;
    @autoreleasepool {
      static NSString *code =
          @"#include <metal_stdlib>\nusing namespace metal;\n"
           "struct P{float "
           "exposure,contrast,gamma,lift,gain,offset,temperature,tint,"
           "saturation,vibrance,hue;};\n"
           "kernel void grade(device const float4* i[[buffer(0)]],device "
           "float4* o[[buffer(1)]],constant P&p[[buffer(2)]],constant "
           "uint&n[[buffer(3)]],uint "
           "k[[thread_position_in_grid]]){if(k>=n)return;float4 c=i[k];float3 "
           "x=c.rgb;float t=p.temperature*.1; "
           "x.r+=t;x.b-=t;x.g+=p.tint*.1;float "
           "l=dot(x,float3(.2126,.7152,.0722));float "
           "v=1+p.vibrance*(1-(max(x.r,max(x.g,x.b))-min(x.r,min(x.g,x.b))));x="
           "l+(x-l)*(p.saturation*v);x=(x-.5)*p.contrast+.5;x=(x+p.lift)*p."
           "gain+p.offset;x*=exp2(p.exposure);x=sign(x)*pow(abs(x),float3(1/"
           "max(.001,p.gamma)));float "
           "a=p.hue*.0174532925199433,co=cos(a),s=sin(a);float3 "
           "r=x;x=float3((.213+co*.787-s*.213)*r.r+(.715-co*.715-s*.715)*r.g+(."
           "072-co*.072+s*.928)*r.b,(.213-co*.213+s*.143)*r.r+(.715+co*.285+s*."
           "140)*r.g+(.072-co*.072-s*.283)*r.b,(.213-co*.213-s*.787)*r.r+(.715-"
           "co*.715+s*.715)*r.g+(.072+co*.928+s*.072)*r.b);o[k]=float4(x,c.a);"
           "}";
      NSError *error = nil;
      id<MTLLibrary> library = [device_ newLibraryWithSource:code
                                                     options:nil
                                                       error:&error];
      id<MTLFunction> function = [library newFunctionWithName:@"grade"];
      id<MTLComputePipelineState> pipeline =
          function ? [device_ newComputePipelineStateWithFunction:function
                                                            error:&error]
                   : nil;
      id<MTLCommandQueue> queue = [device_ newCommandQueue];
      const NSUInteger bytes = source.size_bytes();
      id<MTLBuffer> input =
          [device_ newBufferWithBytes:source.data()
                               length:bytes
                              options:MTLResourceStorageModeShared];
      id<MTLBuffer> output =
          [device_ newBufferWithLength:bytes
                               options:MTLResourceStorageModeShared];
      if (!pipeline || !queue || !input || !output)
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      provenance_.source_upload_performed = true;
      id<MTLCommandBuffer> command = [queue commandBuffer];
      id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
      uint32_t count = 0;
      if (!checked_size_to_uint32(source.size(), count))
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      [encoder setComputePipelineState:pipeline];
      [encoder setBuffer:input offset:0 atIndex:0];
      [encoder setBuffer:output offset:0 atIndex:1];
      [encoder setBytes:&p length:sizeof(p) atIndex:2];
      [encoder setBytes:&count length:sizeof(count) atIndex:3];
      const NSUInteger group =
          std::min<NSUInteger>(pipeline.maxTotalThreadsPerThreadgroup, 64);
      [encoder dispatchThreads:MTLSizeMake(count, 1, 1)
          threadsPerThreadgroup:MTLSizeMake(group, 1, 1)];
      provenance_.command_recorded = true;
      provenance_.dispatch_or_draw_issued = true;
      [encoder endEncoding];
      [command commit];
      [command waitUntilCompleted];
      provenance_.queue_submission_issued = true;
      provenance_.synchronization_waited = true;
      if (command.status != MTLCommandBufferStatusCompleted)
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      std::memcpy(out.data(), output.contents, bytes);
      provenance_.output_written = true;
      provenance_.readback_performed = true;
      provenance_.cpu_color_reference_invocations =
          cpu_color_reference_count() -
          provenance_.cpu_color_reference_invocations;
      return DIGITOR_RESULT_OK;
    }
  }

  DigitorResult
  execute_curves_rgba32f(std::span<const Color> source, std::span<Color> out,
                         const CompiledRgbCurves &compiled) noexcept override {
    begin_grade_provenance(DIGITOR_RENDERER_METAL, true, info_.device_name,
                           "Metal runtime compiler", "rgb-curves-msl-v1:curves",
                           "MTLComputePipelineState:rgb-curves-v1");
    if (source.size() != out.size())
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (source.empty())
      return DIGITOR_RESULT_OK;
    @autoreleasepool {
      static NSString *code =
          @"#include <metal_stdlib>\nusing namespace metal;\n"
           "struct M{float lo,hi,first,last,sb,sa;uint extrap,enabled;};struct "
           "P{M m[4];uint size,count;};"
           "float cv(device const float*l,constant P&p,uint k,float x){M "
           "m=p.m[k];if(!m.enabled||!isfinite(x))return x;if(x<m.lo)return "
           "m.extrap==2?m.first+m.sb*(x-m.lo):m.first;if(x>m.hi)return "
           "m.extrap==2?m.last+m.sa*(x-m.hi):m.last;float "
           "u=(x-m.lo)/(m.hi-m.lo)*float(p.size-1);uint "
           "a=min(uint(u),p.size-1),b=min(a+1,p.size-1);return "
           "mix(l[k*p.size+a],l[k*p.size+b],u-float(a));}"
           "kernel void curves(device const float4*i[[buffer(0)]],device "
           "float4*o[[buffer(1)]],device const float*l[[buffer(2)]],constant "
           "P&p[[buffer(3)]],uint "
           "k[[thread_position_in_grid]]){if(k>=p.count)return;float4 "
           "c=i[k];float "
           "a=c.a;c.r=cv(l,p,0,c.r);c.g=cv(l,p,0,c.g);c.b=cv(l,p,0,c.b);c.r=cv("
           "l,p,1,c.r);c.g=cv(l,p,2,c.g);c.b=cv(l,p,3,c.b);c.a=a;o[k]=c;}";
      struct M {
        float lo, hi, first, last, sb, sa;
        uint32_t extrap, enabled;
      };
      struct P {
        M m[4];
        uint32_t size, count;
      } p{};
      std::vector<float> lut;
      lut.reserve(size_t(compiled.lut_size()) * 4);
      for (unsigned k = 0; k < 4; k++) {
        const auto &c = compiled.curves()[k];
        p.m[k] = {c.domain_min,
                  c.domain_max,
                  c.first_value,
                  c.last_value,
                  c.slope_before,
                  c.slope_after,
                  static_cast<uint32_t>(c.extrapolation),
                  c.enabled && !c.identity ? 1u : 0u};
        lut.insert(lut.end(), c.samples.begin(), c.samples.end());
      }
      p.size = compiled.lut_size();
      if (!checked_size_to_uint32(source.size(), p.count))
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      NSError *error = nil;
      id<MTLLibrary> lib = [device_ newLibraryWithSource:code
                                                 options:nil
                                                   error:&error];
      id<MTLFunction> fn = [lib newFunctionWithName:@"curves"];
      id<MTLComputePipelineState> pipe =
          fn ? [device_ newComputePipelineStateWithFunction:fn error:&error]
             : nil;
      id<MTLCommandQueue> queue = [device_ newCommandQueue];
      id<MTLBuffer> in =
          [device_ newBufferWithBytes:source.data()
                               length:source.size_bytes()
                              options:MTLResourceStorageModeShared];
      id<MTLBuffer> dst =
          [device_ newBufferWithLength:out.size_bytes()
                               options:MTLResourceStorageModeShared];
      id<MTLBuffer> l =
          [device_ newBufferWithBytes:lut.data()
                               length:lut.size() * sizeof(float)
                              options:MTLResourceStorageModeShared];
      if (!pipe || !queue || !in || !dst || !l)
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      provenance_.source_upload_performed = true;
      provenance_.curves_enabled = true;
      provenance_.curve_lut_size = compiled.lut_size();
      provenance_.compiled_curve_identity = compiled.identity();
      provenance_.native_curve_shader_identity = "rgb-curves-msl-v1:curves";
      provenance_.native_lut_resource_identity =
          compiled.identity() + ":" + info_.device_name;
      provenance_.native_lut_cache = CacheDisposition::Miss;
      provenance_.curve_source_bound = true;
      provenance_.curve_destination_bound = true;
      provenance_.curve_lut_bound = true;
      provenance_.curve_parameters_bound = true;
      id<MTLCommandBuffer> command = [queue commandBuffer];
      id<MTLComputeCommandEncoder> e = [command computeCommandEncoder];
      [e setComputePipelineState:pipe];
      [e setBuffer:in offset:0 atIndex:0];
      [e setBuffer:dst offset:0 atIndex:1];
      [e setBuffer:l offset:0 atIndex:2];
      [e setBytes:&p length:sizeof(p) atIndex:3];
      NSUInteger group =
          std::min<NSUInteger>(pipe.maxTotalThreadsPerThreadgroup, 64);
      [e dispatchThreads:MTLSizeMake(p.count, 1, 1)
          threadsPerThreadgroup:MTLSizeMake(group, 1, 1)];
      provenance_.command_recorded = provenance_.dispatch_or_draw_issued = true;
      [e endEncoding];
      [command commit];
      provenance_.queue_submission_issued = true;
      [command waitUntilCompleted];
      provenance_.synchronization_waited = true;
      if (command.status != MTLCommandBufferStatusCompleted)
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      std::memcpy(out.data(), dst.contents, out.size_bytes());
      provenance_.output_written = provenance_.readback_performed = true;
      provenance_.validation_readback_completed = true;
      return DIGITOR_RESULT_OK;
    }
  }

  void destroy_texture(void *object) noexcept override {
    release_native(object);
  }
  void destroy_buffer(void *object) noexcept override {
    release_native(object);
  }
  void destroy_sampler(void *object) noexcept override {
    release_native(object);
  }

private:
  __strong id<MTLDevice> device_;
  NativePipelineCache pipeline_cache_{8};
  DigitorRendererInfo info_{};
};

} // namespace

std::unique_ptr<IRenderBackend>
create_native_backend(DigitorRendererBackend backend) {
  if (backend != DIGITOR_RENDERER_METAL)
    return nullptr;
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  return device != nil ? std::make_unique<MetalBackend>(device) : nullptr;
}

} // namespace digitor
