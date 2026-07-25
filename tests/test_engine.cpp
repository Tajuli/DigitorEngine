#ifdef NDEBUG
#undef NDEBUG
#endif
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

void test_platform_orders() {
    const auto verify = [](digitor::HostPlatform platform,
                           std::vector<DigitorRendererBackend> expected) {
        std::vector<DigitorRendererBackend> attempts;
        auto backend = digitor::select_gpu_backend(platform, DIGITOR_RENDERER_AUTO,
            [&](auto candidate) -> std::unique_ptr<digitor::IRenderBackend> {
                attempts.push_back(candidate);
                return nullptr;
            });
        assert(!backend);
        assert(attempts == expected);
    };
    verify(digitor::HostPlatform::Windows,
           {DIGITOR_RENDERER_VULKAN, DIGITOR_RENDERER_D3D12});
    verify(digitor::HostPlatform::Android,
           {DIGITOR_RENDERER_VULKAN, DIGITOR_RENDERER_OPENGL_ES});
    verify(digitor::HostPlatform::MacOS, {DIGITOR_RENDERER_METAL});
    verify(digitor::HostPlatform::IOS, {DIGITOR_RENDERER_METAL});
}

void test_explicit_backend_is_strict() {
    std::vector<DigitorRendererBackend> attempts;
    auto backend = digitor::select_gpu_backend(digitor::HostPlatform::Windows,
        DIGITOR_RENDERER_D3D12, [&](auto candidate) -> std::unique_ptr<digitor::IRenderBackend> {
            attempts.push_back(candidate);
            return nullptr;
        });
    assert(!backend);
    assert((attempts == std::vector<DigitorRendererBackend>{DIGITOR_RENDERER_D3D12}));

    backend = digitor::select_gpu_backend(digitor::HostPlatform::Android,
        DIGITOR_RENDERER_D3D12, [&](auto) -> std::unique_ptr<digitor::IRenderBackend> {
            assert(false && "unsupported API must not be probed");
            return nullptr;
        });
    assert(!backend);

    backend = digitor::select_gpu_backend(digitor::HostPlatform::MacOS,
        DIGITOR_RENDERER_CPU, [&](auto) -> std::unique_ptr<digitor::IRenderBackend> {
            assert(false && "CPU selection must not probe a GPU");
            return nullptr;
        });
    assert(!backend);
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
    assert(std::strcmp(digitor_get_version(), "0.2.0") == 0);
    test_preferred_backend_selection();
    test_unavailable_backend_fallback();
    test_platform_orders();
    test_explicit_backend_is_strict();

    // Explicit CPU selection is independent of the fallback setting.
    DigitorEngineConfig config{DIGITOR_RENDERER_CPU, 0, 0};
    assert(digitor_initialize(&config) == DIGITOR_RESULT_OK);
    DigitorRendererInfo info{};
    assert(digitor_get_renderer_info(&info) == DIGITOR_RESULT_OK);
    assert(info.backend == DIGITOR_RENDERER_CPU && !info.is_gpu && info.supports_compute);
    assert(!info.supports_fp16 && info.supports_fp32);
    assert(std::strcmp(info.backend_name, "CPU Reference Renderer") == 0);
    assert(std::strcmp(info.device_name, "Host CPU") == 0);
    DigitorRenderContext* context = nullptr;
    assert(digitor_create_render_context(&context) == DIGITOR_RESULT_OK);
    assert(digitor_destroy_render_context(context) == DIGITOR_RESULT_OK);
    context = nullptr;
    assert(digitor_create_render_context(&context) == DIGITOR_RESULT_OK);
    assert(digitor_shutdown() == DIGITOR_RESULT_OK);
    // Shutdown retires live internal contexts; their opaque wrappers remain
    // safe to pass to destroy and report that they are no longer registered.
    assert(digitor_destroy_render_context(context) == DIGITOR_RESULT_INVALID_ARGUMENT);

    DigitorEngineConfig no_fallback{DIGITOR_RENDERER_AUTO, 0, 0};
    assert(digitor_initialize(&no_fallback) == DIGITOR_RESULT_BACKEND_UNAVAILABLE);
    DigitorEngineConfig invalid{static_cast<DigitorRendererBackend>(99), 0, 1};
    assert(digitor_initialize(&invalid) == DIGITOR_RESULT_INVALID_ARGUMENT);
    std::cout << "All DigitorEngine GPU device layer tests passed.\n";
}
