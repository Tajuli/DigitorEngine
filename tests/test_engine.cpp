#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "digitor/digitor.h"
#include "gpu/gpu_backend.hpp"

namespace {
class FakeBackend final : public digitor::IRenderBackend {
public:
    explicit FakeBackend(DigitorRendererBackend backend) { info_.backend = backend; }
    bool initialize(bool) override { return true; }
    void shutdown() noexcept override {}
    DigitorRendererInfo info() const noexcept override { return info_; }
private:
    DigitorRendererInfo info_{};
};

void test_preferred_backend_selection() {
    std::vector<DigitorRendererBackend> attempts;
    auto backend = digitor::select_gpu_backend(digitor::HostPlatform::Windows,
        DIGITOR_RENDERER_AUTO, [&](auto candidate) -> std::unique_ptr<digitor::IRenderBackend> {
            attempts.push_back(candidate);
            return std::make_unique<FakeBackend>(candidate);
        });
    assert(backend && backend->info().backend == DIGITOR_RENDERER_VULKAN);
    assert(attempts.size() == 1);
}

void test_unavailable_backend_fallback() {
    std::vector<DigitorRendererBackend> attempts;
    auto backend = digitor::select_gpu_backend(digitor::HostPlatform::Android,
        DIGITOR_RENDERER_AUTO, [&](auto candidate) -> std::unique_ptr<digitor::IRenderBackend> {
            attempts.push_back(candidate);
            if (candidate == DIGITOR_RENDERER_OPENGL_ES) return std::make_unique<FakeBackend>(candidate);
            return nullptr;
        });
    assert(backend && backend->info().backend == DIGITOR_RENDERER_OPENGL_ES);
    assert((attempts == std::vector<DigitorRendererBackend>{DIGITOR_RENDERER_VULKAN, DIGITOR_RENDERER_OPENGL_ES}));
}
}

int main() {
    assert(std::strcmp(digitor_get_version(), "0.4.0") == 0);
    test_preferred_backend_selection();
    test_unavailable_backend_fallback();

    DigitorEngineConfig config{DIGITOR_RENDERER_CPU, 0, 1};
    assert(digitor_initialize(&config) == DIGITOR_RESULT_OK);
    DigitorRendererInfo info{};
    assert(digitor_get_renderer_info(&info) == DIGITOR_RESULT_OK);
    assert(info.backend == DIGITOR_RENDERER_CPU && !info.is_gpu && info.supports_compute);
    DigitorRenderContext* context = nullptr;
    assert(digitor_create_render_context(&context) == DIGITOR_RESULT_OK);
    assert(digitor_shutdown() == DIGITOR_RESULT_RESOURCE_IN_USE);

    DigitorTextureDesc texture_desc{1920, 1080, DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
                                    DIGITOR_TEXTURE_USAGE_STORAGE | DIGITOR_TEXTURE_USAGE_RENDER_TARGET};
    DigitorTexture* texture = nullptr;
    assert(digitor_create_texture(context, &texture_desc, &texture) == DIGITOR_RESULT_OK);
    assert(texture != nullptr);
    assert(digitor_destroy_render_context(context) == DIGITOR_RESULT_RESOURCE_IN_USE);
    assert(digitor_destroy_texture(texture) == DIGITOR_RESULT_OK);
    assert(digitor_destroy_texture(texture) == DIGITOR_RESULT_INVALID_ARGUMENT);

    DigitorBufferDesc buffer_desc{4096, DIGITOR_BUFFER_USAGE_UNIFORM | DIGITOR_BUFFER_USAGE_UPLOAD};
    DigitorBuffer* buffer = nullptr;
    assert(digitor_create_buffer(context, &buffer_desc, &buffer) == DIGITOR_RESULT_OK);
    assert(digitor_destroy_buffer(buffer) == DIGITOR_RESULT_OK);
    assert(digitor_destroy_buffer(buffer) == DIGITOR_RESULT_INVALID_ARGUMENT);

    DigitorSamplerDesc sampler_desc{DIGITOR_FILTER_LINEAR, DIGITOR_FILTER_LINEAR,
        DIGITOR_FILTER_NEAREST, DIGITOR_ADDRESS_CLAMP_TO_EDGE, DIGITOR_ADDRESS_REPEAT,
        DIGITOR_ADDRESS_MIRRORED_REPEAT, 1};
    DigitorSampler* sampler = nullptr;
    assert(digitor_create_sampler(context, &sampler_desc, &sampler) == DIGITOR_RESULT_OK);
    assert(digitor_destroy_sampler(sampler) == DIGITOR_RESULT_OK);
    assert(digitor_destroy_sampler(sampler) == DIGITOR_RESULT_INVALID_ARGUMENT);

    DigitorTextureDesc invalid_texture{0, 1080, DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
                                       DIGITOR_TEXTURE_USAGE_SAMPLED};
    assert(digitor_create_texture(context, &invalid_texture, &texture) == DIGITOR_RESULT_INVALID_ARGUMENT);
    DigitorTextureDesc unsupported{1, 1, static_cast<DigitorPixelFormat>(99), DIGITOR_TEXTURE_USAGE_SAMPLED};
    assert(digitor_create_texture(context, &unsupported, &texture) == DIGITOR_RESULT_UNSUPPORTED);
    DigitorBufferDesc zero_buffer{0, DIGITOR_BUFFER_USAGE_STORAGE};
    assert(digitor_create_buffer(context, &zero_buffer, &buffer) == DIGITOR_RESULT_INVALID_ARGUMENT);
    assert(digitor_create_buffer(context, nullptr, &buffer) == DIGITOR_RESULT_INVALID_ARGUMENT);
    assert(digitor_destroy_render_context(context) == DIGITOR_RESULT_OK);
    assert(digitor_shutdown() == DIGITOR_RESULT_OK);

    DigitorEngineConfig no_fallback{DIGITOR_RENDERER_CPU, 0, 0};
    assert(digitor_initialize(&no_fallback) == DIGITOR_RESULT_BACKEND_UNAVAILABLE);
    std::cout << "All DigitorEngine GPU device layer tests passed.\n";
}
