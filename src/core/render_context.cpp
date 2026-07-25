#include "core/render_context.hpp"

#include <limits>
#include <new>

#include "core/resources.hpp"
#include "gpu/gpu_backend.hpp"

namespace digitor {
namespace {
constexpr uint32_t kTextureUsageMask = DIGITOR_TEXTURE_USAGE_SAMPLED |
    DIGITOR_TEXTURE_USAGE_STORAGE | DIGITOR_TEXTURE_USAGE_RENDER_TARGET |
    DIGITOR_TEXTURE_USAGE_TRANSFER_SOURCE | DIGITOR_TEXTURE_USAGE_TRANSFER_DESTINATION;
constexpr uint32_t kBufferUsageMask = DIGITOR_BUFFER_USAGE_UNIFORM |
    DIGITOR_BUFFER_USAGE_STORAGE | DIGITOR_BUFFER_USAGE_UPLOAD | DIGITOR_BUFFER_USAGE_STAGING;
}

DigitorRendererBackend RenderContext::backend_info() const noexcept { return backend_.info().backend; }

DigitorResult RenderContext::create_texture(const DigitorTextureDesc& desc, Texture** out_texture) {
    if (out_texture == nullptr || desc.width == 0 || desc.height == 0 || desc.usage == 0 ||
        (desc.usage & ~kTextureUsageMask) != 0) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    *out_texture = nullptr;
    std::size_t bytes_per_pixel = 0;
    switch (desc.format) {
        case DIGITOR_PIXEL_FORMAT_RGBA8_UNORM:
        case DIGITOR_PIXEL_FORMAT_BGRA8_UNORM: bytes_per_pixel = 4; break;
        case DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT: bytes_per_pixel = 8; break;
        case DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT: bytes_per_pixel = 16; break;
        default: return DIGITOR_RESULT_UNSUPPORTED;
    }
    if (desc.width > std::numeric_limits<std::size_t>::max() / bytes_per_pixel / desc.height) {
        return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    void* native = nullptr;
    if (backend_info() != DIGITOR_RENDERER_CPU) {
        const auto result = backend_.create_texture(desc, &native);
        if (result != DIGITOR_RESULT_OK) return result;
        if (!native) return DIGITOR_RESULT_INTERNAL_ERROR;
    }
    try {
        *out_texture = new Texture(*this, desc, backend_info() == DIGITOR_RENDERER_CPU ?
            static_cast<std::size_t>(desc.width) * desc.height * bytes_per_pixel : 0, native);
    } catch (const std::bad_alloc&) {
        backend_.destroy_texture(native);
        return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    return DIGITOR_RESULT_OK;
}

DigitorResult RenderContext::create_buffer(const DigitorBufferDesc& desc, Buffer** out_buffer) {
    if (out_buffer == nullptr || desc.size == 0 || desc.usage == 0 ||
        (desc.usage & ~kBufferUsageMask) != 0 || desc.size > std::numeric_limits<std::size_t>::max()) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    *out_buffer = nullptr;
    void* native = nullptr;
    if (backend_info() != DIGITOR_RENDERER_CPU) {
        const auto result = backend_.create_buffer(desc, &native);
        if (result != DIGITOR_RESULT_OK) return result;
        if (!native) return DIGITOR_RESULT_INTERNAL_ERROR;
    }
    try {
        *out_buffer = new Buffer(*this, desc, native);
    } catch (const std::bad_alloc&) {
        backend_.destroy_buffer(native);
        return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    return DIGITOR_RESULT_OK;
}

DigitorResult RenderContext::create_sampler(const DigitorSamplerDesc& desc, Sampler** out_sampler) {
    if (!out_sampler) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_sampler = nullptr;
    const auto valid_filter=[](DigitorFilter f){ return f==DIGITOR_FILTER_NEAREST || f==DIGITOR_FILTER_LINEAR; };
    const auto valid_address=[](DigitorAddressMode m){ return m>=DIGITOR_ADDRESS_CLAMP_TO_EDGE && m<=DIGITOR_ADDRESS_MIRRORED_REPEAT; };
    if (!valid_filter(desc.min_filter) || !valid_filter(desc.mag_filter) || !valid_filter(desc.mip_filter) ||
        !valid_address(desc.address_u) || !valid_address(desc.address_v) || !valid_address(desc.address_w) ||
        desc.normalized_coordinates > 1) return DIGITOR_RESULT_INVALID_ARGUMENT;
    void* native = nullptr;
    if (backend_info() != DIGITOR_RENDERER_CPU) {
        auto result=backend_.create_sampler(desc, &native); if (result != DIGITOR_RESULT_OK) return result;
        if (!native) return DIGITOR_RESULT_INTERNAL_ERROR;
    }
    try { *out_sampler = new Sampler(*this, desc, native); }
    catch (const std::bad_alloc&) { backend_.destroy_sampler(native); return DIGITOR_RESULT_OUT_OF_MEMORY; }
    return DIGITOR_RESULT_OK;
}

}  // namespace digitor
