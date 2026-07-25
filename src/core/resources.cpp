#include "core/resources.hpp"

#include "core/render_context.hpp"

namespace digitor {

Texture::Texture(RenderContext& owner, const DigitorTextureDesc& desc, std::size_t byte_size)
    : owner_(owner), desc_(desc), storage_(byte_size) {
    owner_.retain_resource();
}

Texture::~Texture() { owner_.release_resource(); }

Buffer::Buffer(RenderContext& owner, const DigitorBufferDesc& desc)
    : owner_(owner), desc_(desc), storage_(static_cast<std::size_t>(desc.size)) {
    owner_.retain_resource();
}

Buffer::~Buffer() { owner_.release_resource(); }

}  // namespace digitor
