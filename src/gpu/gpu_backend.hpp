#pragma once

#include <memory>
#include <functional>
#include <cstddef>

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
    virtual void destroy_texture(void*) noexcept;
    virtual void destroy_buffer(void*) noexcept;
    virtual void destroy_sampler(void*) noexcept;
};

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
