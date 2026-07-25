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

Buffer::~Buffer() { owner_.backend_object().destroy_buffer(native_); owner_.release_resource(); }

Sampler::Sampler(RenderContext& owner, const DigitorSamplerDesc& desc, void* native)
    : owner_(owner), desc_(desc), native_(native) { owner_.retain_resource(); }
Sampler::~Sampler() { owner_.backend_object().destroy_sampler(native_); owner_.release_resource(); }

}  // namespace digitor
