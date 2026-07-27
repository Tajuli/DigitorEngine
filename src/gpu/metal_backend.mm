#import <Metal/Metal.h>

#include <algorithm>
#include <cstring>
#include <limits>

#include "gpu/gpu_backend.hpp"
#include "core/string_utils.hpp"

namespace digitor {
namespace {

MTLPixelFormat metal_format(DigitorPixelFormat format) noexcept {
    switch (format) {
        case DIGITOR_PIXEL_FORMAT_RGBA8_UNORM: return MTLPixelFormatRGBA8Unorm;
        case DIGITOR_PIXEL_FORMAT_BGRA8_UNORM: return MTLPixelFormatBGRA8Unorm;
        case DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT: return MTLPixelFormatRGBA16Float;
        case DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT: return MTLPixelFormatRGBA32Float;
        default: return MTLPixelFormatInvalid;
    }
}



// Resource creation uses __bridge_retained exactly once. This is its matching
// transfer back to ARC, which releases the object at the end of the scope.
void release_native(void* object) noexcept {
    if (object != nullptr) {
        (void)CFBridgingRelease(object);
    }
}

class MetalBackend final : public IRenderBackend {
public:
    explicit MetalBackend(id<MTLDevice> device) : device_(device) {
        info_.backend = DIGITOR_RENDERER_METAL;
        copy_bounded(info_.backend_name, "Metal");
        copy_bounded(info_.device_name, device.name.UTF8String != nullptr
            ? std::string_view(device.name.UTF8String) : std::string_view{});
        info_.is_gpu = 1;
        info_.supports_compute = 1;
        info_.supports_fp16 = 1;
        info_.supports_fp32 = 1;
    }

    bool initialize(bool) override { return device_ != nil; }
    void shutdown() noexcept override {}
    DigitorRendererInfo info() const noexcept override { return info_; }

    DigitorResult create_texture(const DigitorTextureDesc& description, void** out) noexcept override {
        if (out == nullptr) return DIGITOR_RESULT_INVALID_ARGUMENT;
        *out = nullptr;
        @autoreleasepool {
            const MTLPixelFormat format = metal_format(description.format);
            if (device_ == nil || format == MTLPixelFormatInvalid || description.width == 0 ||
                description.height == 0) return DIGITOR_RESULT_INVALID_ARGUMENT;
            MTLTextureDescriptor* descriptor =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                   width:description.width
                                                                  height:description.height
                                                               mipmapped:NO];
            descriptor.storageMode = MTLStorageModePrivate;
            descriptor.usage = 0;
            if (description.usage & DIGITOR_TEXTURE_USAGE_SAMPLED) descriptor.usage |= MTLTextureUsageShaderRead;
            if (description.usage & DIGITOR_TEXTURE_USAGE_STORAGE) descriptor.usage |= MTLTextureUsageShaderWrite;
            if (description.usage & DIGITOR_TEXTURE_USAGE_RENDER_TARGET) descriptor.usage |= MTLTextureUsageRenderTarget;
            id<MTLTexture> texture = [device_ newTextureWithDescriptor:descriptor];
            if (texture == nil) return DIGITOR_RESULT_OUT_OF_MEMORY;
            *out = (__bridge_retained void*)texture;
            return DIGITOR_RESULT_OK;
        }
    }

    DigitorResult create_buffer(const DigitorBufferDesc& description, void** out) noexcept override {
        if (out == nullptr) return DIGITOR_RESULT_INVALID_ARGUMENT;
        *out = nullptr;
        @autoreleasepool {
            if (device_ == nil || description.size == 0) return DIGITOR_RESULT_INVALID_ARGUMENT;
            if constexpr (sizeof(NSUInteger) < sizeof(description.size)) {
                if (description.size > std::numeric_limits<NSUInteger>::max()) {
                    return DIGITOR_RESULT_INVALID_ARGUMENT;
                }
            }
            const bool host_visible =
                (description.usage & (DIGITOR_BUFFER_USAGE_UPLOAD | DIGITOR_BUFFER_USAGE_STAGING)) != 0;
            const MTLResourceOptions options = host_visible ? MTLResourceStorageModeShared
                                                            : MTLResourceStorageModePrivate;
            id<MTLBuffer> buffer = [device_ newBufferWithLength:static_cast<NSUInteger>(description.size)
                                                       options:options];
            if (buffer == nil) return DIGITOR_RESULT_OUT_OF_MEMORY;
            *out = (__bridge_retained void*)buffer;
            return DIGITOR_RESULT_OK;
        }
    }

    DigitorResult create_sampler(const DigitorSamplerDesc& description, void** out) noexcept override {
        if (out == nullptr) return DIGITOR_RESULT_INVALID_ARGUMENT;
        *out = nullptr;
        @autoreleasepool {
            if (device_ == nil) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
            MTLSamplerDescriptor* descriptor = [[MTLSamplerDescriptor alloc] init];
            descriptor.minFilter = description.min_filter == DIGITOR_FILTER_LINEAR
                ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
            descriptor.magFilter = description.mag_filter == DIGITOR_FILTER_LINEAR
                ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
            descriptor.normalizedCoordinates = description.normalized_coordinates != 0;
            id<MTLSamplerState> sampler = [device_ newSamplerStateWithDescriptor:descriptor];
            if (sampler == nil) return DIGITOR_RESULT_OUT_OF_MEMORY;
            *out = (__bridge_retained void*)sampler;
            return DIGITOR_RESULT_OK;
        }
    }

    DigitorResult map_buffer(void* object, uint64_t offset, uint64_t, void** out) noexcept override {
        if (out == nullptr) return DIGITOR_RESULT_INVALID_ARGUMENT;
        *out = nullptr;
        if (object == nullptr) return DIGITOR_RESULT_INVALID_ARGUMENT;
        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)object;
        if (offset > buffer.length) return DIGITOR_RESULT_INVALID_ARGUMENT;
        void* contents = buffer.contents;
        if (contents == nullptr) return DIGITOR_RESULT_UNSUPPORTED;
        *out = static_cast<uint8_t*>(contents) + static_cast<std::size_t>(offset);
        return DIGITOR_RESULT_OK;
    }

    void unmap_buffer(void*) noexcept override {}

    DigitorResult render_rgba8(uint32_t width, uint32_t height,
                               std::span<const uint8_t> source,
                               std::vector<uint8_t>& out) noexcept override {
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
            if (!source.empty() && source.size() != byte_count) return DIGITOR_RESULT_INVALID_ARGUMENT;

            MTLTextureDescriptor* descriptor =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                   width:width height:height mipmapped:NO];
            descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            descriptor.storageMode = MTLStorageModeShared;
            id<MTLTexture> target = [device_ newTextureWithDescriptor:descriptor];
            id<MTLCommandQueue> queue = [device_ newCommandQueue];
            if (target == nil || queue == nil) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
            id<MTLCommandBuffer> command_buffer = [queue commandBuffer];
            if (command_buffer == nil) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

            if (source.empty()) {
                MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture = target;
                pass.colorAttachments[0].loadAction = MTLLoadActionClear;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
                id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:pass];
                if (encoder == nil) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
                [encoder endEncoding];
            } else {
                [target replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0
                              withBytes:source.data() bytesPerRow:static_cast<NSUInteger>(width) * 4];
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
            [target getBytes:out.data() bytesPerRow:static_cast<NSUInteger>(width) * 4
                    fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];
            return DIGITOR_RESULT_OK;
        }
    }

    DigitorResult grade_rgba32f(std::span<const Color> source, std::span<Color> out,
                                const ColorGrade& p) noexcept override {
        begin_grade_provenance(DIGITOR_RENDERER_METAL, true, info_.device_name,
                               "Metal runtime compiler", "grade-msl-v1:grade",
                               "MTLComputePipelineState:grade-v1");
        if (gpu_failure_point() != GpuFailurePoint::None)
            return injected_failure(gpu_failure_point());
        if (source.size() != out.size()) return DIGITOR_RESULT_INVALID_ARGUMENT;
        if (source.empty()) return DIGITOR_RESULT_OK;
        @autoreleasepool {
            static NSString* code = @"#include <metal_stdlib>\nusing namespace metal;\n"
                "struct P{float exposure,contrast,gamma,lift,gain,offset,temperature,tint,saturation,vibrance,hue;};\n"
                "kernel void grade(device const float4* i[[buffer(0)]],device float4* o[[buffer(1)]],constant P&p[[buffer(2)]],constant uint&n[[buffer(3)]],uint k[[thread_position_in_grid]]){if(k>=n)return;float4 c=i[k];float3 x=c.rgb;float t=p.temperature*.1; x.r+=t;x.b-=t;x.g+=p.tint*.1;float l=dot(x,float3(.2126,.7152,.0722));float v=1+p.vibrance*(1-(max(x.r,max(x.g,x.b))-min(x.r,min(x.g,x.b))));x=l+(x-l)*(p.saturation*v);x=(x-.5)*p.contrast+.5;x=(x+p.lift)*p.gain+p.offset;x*=exp2(p.exposure);x=sign(x)*pow(abs(x),float3(1/max(.001,p.gamma)));float a=p.hue*.0174532925199433,co=cos(a),s=sin(a);float3 r=x;x=float3((.213+co*.787-s*.213)*r.r+(.715-co*.715-s*.715)*r.g+(.072-co*.072+s*.928)*r.b,(.213-co*.213+s*.143)*r.r+(.715+co*.285+s*.140)*r.g+(.072-co*.072-s*.283)*r.b,(.213-co*.213-s*.787)*r.r+(.715-co*.715+s*.715)*r.g+(.072+co*.928+s*.072)*r.b);o[k]=float4(x,c.a);}";
            NSError* error = nil;
            id<MTLLibrary> library = [device_ newLibraryWithSource:code options:nil error:&error];
            id<MTLFunction> function = [library newFunctionWithName:@"grade"];
            id<MTLComputePipelineState> pipeline = function ? [device_ newComputePipelineStateWithFunction:function error:&error] : nil;
            id<MTLCommandQueue> queue = [device_ newCommandQueue];
            const NSUInteger bytes = source.size_bytes();
            id<MTLBuffer> input = [device_ newBufferWithBytes:source.data() length:bytes options:MTLResourceStorageModeShared];
            id<MTLBuffer> output = [device_ newBufferWithLength:bytes options:MTLResourceStorageModeShared];
            if (!pipeline || !queue || !input || !output) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
            provenance_.source_upload_performed = true;
            id<MTLCommandBuffer> command = [queue commandBuffer];
            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            uint32_t count = static_cast<uint32_t>(source.size());
            [encoder setComputePipelineState:pipeline]; [encoder setBuffer:input offset:0 atIndex:0];
            [encoder setBuffer:output offset:0 atIndex:1]; [encoder setBytes:&p length:sizeof(p) atIndex:2];
            [encoder setBytes:&count length:sizeof(count) atIndex:3];
            const NSUInteger group = std::min<NSUInteger>(pipeline.maxTotalThreadsPerThreadgroup, 64);
            [encoder dispatchThreads:MTLSizeMake(count,1,1) threadsPerThreadgroup:MTLSizeMake(group,1,1)];
            provenance_.command_recorded = true; provenance_.dispatch_or_draw_issued = true;
            [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
            provenance_.queue_submission_issued = true; provenance_.synchronization_waited = true;
            if (command.status != MTLCommandBufferStatusCompleted) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
            std::memcpy(out.data(), output.contents, bytes);
            provenance_.output_written = true; provenance_.readback_performed = true;
            provenance_.cpu_color_reference_invocations =
      cpu_color_reference_count() - provenance_.cpu_color_reference_invocations;
            return DIGITOR_RESULT_OK;
        }
    }

    DigitorResult curves_rgba32f(std::span<const Color> source, std::span<Color> out,
                                 const CompiledRgbCurves& compiled) noexcept override {
        begin_grade_provenance(DIGITOR_RENDERER_METAL, true, info_.device_name,
                               "Metal runtime compiler", "rgb-curves-msl-v1:curves",
                               "MTLComputePipelineState:rgb-curves-v1");
        if (gpu_failure_point() != GpuFailurePoint::None) return injected_failure(gpu_failure_point());
        if (source.size() != out.size()) return DIGITOR_RESULT_INVALID_ARGUMENT;
        if (source.empty()) return DIGITOR_RESULT_OK;
        @autoreleasepool {
            static NSString* code = @"#include <metal_stdlib>\nusing namespace metal;\n"
              "struct M{float lo,hi,first,last,sb,sa;uint extrap,enabled;};struct P{M m[4];uint size,count;};"
              "float cv(device const float*l,constant P&p,uint k,float x){M m=p.m[k];if(!m.enabled||!isfinite(x))return x;if(x<m.lo)return m.extrap==2?m.first+m.sb*(x-m.lo):m.first;if(x>m.hi)return m.extrap==2?m.last+m.sa*(x-m.hi):m.last;float u=(x-m.lo)/(m.hi-m.lo)*float(p.size-1);uint a=min(uint(u),p.size-1),b=min(a+1,p.size-1);return mix(l[k*p.size+a],l[k*p.size+b],u-float(a));}"
              "kernel void curves(device const float4*i[[buffer(0)]],device float4*o[[buffer(1)]],device const float*l[[buffer(2)]],constant P&p[[buffer(3)]],uint k[[thread_position_in_grid]]){if(k>=p.count)return;float4 c=i[k];float a=c.a;c.r=cv(l,p,0,c.r);c.g=cv(l,p,0,c.g);c.b=cv(l,p,0,c.b);c.r=cv(l,p,1,c.r);c.g=cv(l,p,2,c.g);c.b=cv(l,p,3,c.b);c.a=a;o[k]=c;}";
            struct M { float lo,hi,first,last,sb,sa; uint32_t extrap,enabled; };
            struct P { M m[4]; uint32_t size,count; } p{};
            std::vector<float> lut; lut.reserve(size_t(compiled.lut_size())*4);
            for (unsigned k=0;k<4;k++) { const auto& c=compiled.curves()[k];
                p.m[k]={c.domain_min,c.domain_max,c.first_value,c.last_value,c.slope_before,c.slope_after,
                        static_cast<uint32_t>(c.extrapolation),c.enabled?1u:0u};
                lut.insert(lut.end(),c.samples.begin(),c.samples.end()); }
            p.size=compiled.lut_size(); p.count=static_cast<uint32_t>(source.size());
            NSError* error=nil; id<MTLLibrary> lib=[device_ newLibraryWithSource:code options:nil error:&error];
            id<MTLFunction> fn=[lib newFunctionWithName:@"curves"];
            id<MTLComputePipelineState> pipe=fn?[device_ newComputePipelineStateWithFunction:fn error:&error]:nil;
            id<MTLCommandQueue> queue=[device_ newCommandQueue];
            id<MTLBuffer> in=[device_ newBufferWithBytes:source.data() length:source.size_bytes() options:MTLResourceStorageModeShared];
            id<MTLBuffer> dst=[device_ newBufferWithLength:out.size_bytes() options:MTLResourceStorageModeShared];
            id<MTLBuffer> l=[device_ newBufferWithBytes:lut.data() length:lut.size()*sizeof(float) options:MTLResourceStorageModeShared];
            if(!pipe||!queue||!in||!dst||!l)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
            provenance_.source_upload_performed=true; provenance_.curves_enabled=true;
            provenance_.curve_lut_size=compiled.lut_size(); provenance_.compiled_curve_identity=compiled.identity();
            provenance_.native_curve_shader_identity="rgb-curves-msl-v1:curves";
            provenance_.native_lut_resource_identity=compiled.identity()+":"+info_.device_name;
            provenance_.native_lut_cache=CacheDisposition::Miss; provenance_.curve_source_bound=true;
            provenance_.curve_destination_bound=true; provenance_.curve_lut_bound=true; provenance_.curve_parameters_bound=true;
            id<MTLCommandBuffer> command=[queue commandBuffer]; id<MTLComputeCommandEncoder> e=[command computeCommandEncoder];
            [e setComputePipelineState:pipe]; [e setBuffer:in offset:0 atIndex:0]; [e setBuffer:dst offset:0 atIndex:1];
            [e setBuffer:l offset:0 atIndex:2]; [e setBytes:&p length:sizeof(p) atIndex:3];
            NSUInteger group=std::min<NSUInteger>(pipe.maxTotalThreadsPerThreadgroup,64);
            [e dispatchThreads:MTLSizeMake(p.count,1,1) threadsPerThreadgroup:MTLSizeMake(group,1,1)];
            provenance_.command_recorded=provenance_.dispatch_or_draw_issued=true; [e endEncoding]; [command commit];
            provenance_.queue_submission_issued=true; [command waitUntilCompleted]; provenance_.synchronization_waited=true;
            if(command.status!=MTLCommandBufferStatusCompleted)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
            std::memcpy(out.data(),dst.contents,out.size_bytes()); provenance_.output_written=provenance_.readback_performed=true;
            provenance_.validation_readback_completed=true;
            return DIGITOR_RESULT_OK;
        }
    }

    void destroy_texture(void* object) noexcept override { release_native(object); }
    void destroy_buffer(void* object) noexcept override { release_native(object); }
    void destroy_sampler(void* object) noexcept override { release_native(object); }

private:
    __strong id<MTLDevice> device_;
    DigitorRendererInfo info_{};
};

}  // namespace

std::unique_ptr<IRenderBackend> create_native_backend(DigitorRendererBackend backend) {
    if (backend != DIGITOR_RENDERER_METAL) return nullptr;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    return device != nil ? std::make_unique<MetalBackend>(device) : nullptr;
}

}  // namespace digitor
