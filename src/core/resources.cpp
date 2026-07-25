#include "core/resources.hpp"

#include "core/render_context.hpp"
#include "gpu/gpu_backend.hpp"

namespace digitor {

Texture::Texture(RenderContext& owner, const DigitorTextureDesc& desc, std::size_t byte_size, void* native)
    : owner_(owner), desc_(desc), storage_(byte_size), native_(native) {
    owner_.retain_resource();
}

Texture::~Texture() { owner_.backend_object().destroy_texture(native_); owner_.release_resource(); }

Buffer::Buffer(RenderContext& owner, const DigitorBufferDesc& desc, void* native)
    : owner_(owner), desc_(desc), storage_(native ? 0 : static_cast<std::size_t>(desc.size)), native_(native) {
    owner_.retain_resource();
}

Buffer::~Buffer() {
    if (mapped_ && native_) owner_.backend_object().unmap_buffer(native_);
    owner_.backend_object().destroy_buffer(native_);
    owner_.release_resource();
}

DigitorResult Buffer::map(uint64_t offset, uint64_t size, void** out_data) noexcept {
    if (!out_data) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_data = nullptr;
    std::scoped_lock lock(map_mutex_);
    if (offset >= desc_.size) return DIGITOR_RESULT_INVALID_ARGUMENT;
    const uint64_t length = size == 0 ? desc_.size - offset : size;
    if (mapped_ || !(desc_.usage & (DIGITOR_BUFFER_USAGE_UPLOAD | DIGITOR_BUFFER_USAGE_STAGING)) ||
        length > desc_.size - offset) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    DigitorResult result = DIGITOR_RESULT_OK;
    if (native_) result = owner_.backend_object().map_buffer(native_, offset, length, out_data);
    else *out_data = storage_.data() + static_cast<std::size_t>(offset);
    if (result == DIGITOR_RESULT_OK && *out_data) mapped_ = true;
    else if (result == DIGITOR_RESULT_OK) result = DIGITOR_RESULT_INTERNAL_ERROR;
    return result;
}

DigitorResult Buffer::unmap() noexcept {
    std::scoped_lock lock(map_mutex_);
    if (!mapped_) return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (native_) owner_.backend_object().unmap_buffer(native_);
    mapped_ = false;
    return DIGITOR_RESULT_OK;
}

Sampler::Sampler(RenderContext& owner, const DigitorSamplerDesc& desc, void* native)
    : owner_(owner), desc_(desc), native_(native) { owner_.retain_resource(); }
Sampler::~Sampler() { owner_.backend_object().destroy_sampler(native_); owner_.release_resource(); }

}  // namespace digitor
