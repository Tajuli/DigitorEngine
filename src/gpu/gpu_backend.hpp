#pragma once

#include <memory>
#include <functional>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "digitor/digitor.h"
#include "platform/platform.hpp"

namespace digitor {

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    virtual bool initialize(bool enable_validation) = 0;
    virtual void shutdown() noexcept = 0;
    [[nodiscard]] virtual DigitorRendererInfo info() const noexcept = 0;
    virtual DigitorResult create_texture(const DigitorTextureDesc&, void** out) noexcept;
    virtual DigitorResult create_buffer(const DigitorBufferDesc&, void** out) noexcept;
    virtual DigitorResult create_sampler(const DigitorSamplerDesc&, void** out) noexcept;
    virtual DigitorResult map_buffer(void*, uint64_t offset, uint64_t size, void** out) noexcept;
    virtual void unmap_buffer(void*) noexcept;
    virtual void destroy_texture(void*) noexcept;
    virtual void destroy_buffer(void*) noexcept;
    virtual void destroy_sampler(void*) noexcept;

    // Records and submits the backend's native clear/copy pass, then reads the
    // RGBA8 render target back for preview.  Keeping this operation internal
    // preserves the v2 C ABI while allowing preview to consume GPU pixels.
    virtual DigitorResult render_rgba8(uint32_t width, uint32_t height,
        std::span<const uint8_t> source, std::vector<uint8_t>& destination) noexcept;
};

[[nodiscard]] bool gpu_validation_requested() noexcept;

std::unique_ptr<IRenderBackend> create_gpu_backend(
    DigitorRendererBackend preferred
);

// Implemented by the platform-specific translation unit (or the portable stub).
std::unique_ptr<IRenderBackend> create_native_backend(DigitorRendererBackend backend);

using BackendFactory = std::function<std::unique_ptr<IRenderBackend>(DigitorRendererBackend)>;

// Internal selection seam used to test platform policy without requiring GPU hardware.
std::unique_ptr<IRenderBackend> select_gpu_backend(
    HostPlatform platform,
    DigitorRendererBackend preferred,
    const BackendFactory& factory);

}  // namespace digitor
