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
