#include "core/render_context.hpp"

#include <limits>
#include <new>

#include "core/resources.hpp"

namespace digitor {
namespace {
constexpr uint32_t kTextureUsageMask = DIGITOR_TEXTURE_USAGE_SAMPLED |
    DIGITOR_TEXTURE_USAGE_STORAGE | DIGITOR_TEXTURE_USAGE_RENDER_TARGET |
    DIGITOR_TEXTURE_USAGE_TRANSFER_SOURCE | DIGITOR_TEXTURE_USAGE_TRANSFER_DESTINATION;
constexpr uint32_t kBufferUsageMask = DIGITOR_BUFFER_USAGE_UNIFORM |
    DIGITOR_BUFFER_USAGE_STORAGE | DIGITOR_BUFFER_USAGE_UPLOAD | DIGITOR_BUFFER_USAGE_STAGING;
}

DigitorResult RenderContext::create_texture(const DigitorTextureDesc& desc, Texture** out_texture) {
    if (out_texture == nullptr || desc.width == 0 || desc.height == 0 || desc.usage == 0 ||
        (desc.usage & ~kTextureUsageMask) != 0 || desc.format != DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    *out_texture = nullptr;
    if (backend_ != DIGITOR_RENDERER_CPU) return DIGITOR_RESULT_UNSUPPORTED;
    constexpr std::size_t bytes_per_pixel = sizeof(float) * 4;
    if (desc.width > std::numeric_limits<std::size_t>::max() / bytes_per_pixel / desc.height) {
        return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    try {
        *out_texture = new Texture(*this, desc,
            static_cast<std::size_t>(desc.width) * desc.height * bytes_per_pixel);
    } catch (const std::bad_alloc&) {
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
    if (backend_ != DIGITOR_RENDERER_CPU) return DIGITOR_RESULT_UNSUPPORTED;
    try {
        *out_buffer = new Buffer(*this, desc);
    } catch (const std::bad_alloc&) {
        return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    return DIGITOR_RESULT_OK;
}

}  // namespace digitor
