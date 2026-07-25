#pragma once

#include <atomic>
#include <cstddef>

#include "digitor/digitor.h"

namespace digitor {

class Texture;
class Buffer;
class Sampler;
class IRenderBackend;

class RenderContext final {
public:
    explicit RenderContext(IRenderBackend& backend) noexcept : backend_(backend) {}

    [[nodiscard]] DigitorRendererBackend backend() const noexcept {
        return backend_info();
    }

    DigitorResult create_texture(const DigitorTextureDesc& desc, Texture** out_texture);
    DigitorResult create_buffer(const DigitorBufferDesc& desc, Buffer** out_buffer);
    DigitorResult create_sampler(const DigitorSamplerDesc& desc, Sampler** out_sampler);
    IRenderBackend& backend_object() noexcept { return backend_; }
    void retain_resource() noexcept { resource_count_.fetch_add(1, std::memory_order_relaxed); }
    void release_resource() noexcept { resource_count_.fetch_sub(1, std::memory_order_relaxed); }
    [[nodiscard]] bool has_resources() const noexcept {
        return resource_count_.load(std::memory_order_acquire) != 0;
    }

private:
    DigitorRendererBackend backend_info() const noexcept;
    IRenderBackend& backend_;
    std::atomic<std::size_t> resource_count_{0};
};

}  // namespace digitor
