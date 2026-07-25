#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "digitor/digitor.h"
#include "gpu/gpu_backend.hpp"
#include "digitor/commands.hpp"
#include "digitor/shader.hpp"
#include "digitor/render_graph.hpp"
#include "digitor/color.hpp"
#include "cpu/cpu_backend.hpp"
#include <cmath>
void test_editor();
void test_v2();

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
    assert(std::strcmp(digitor_get_version(), "3.0.0") == 0);
    test_editor();
    test_v2();

    { digitor::CommandQueue q; digitor::CommandBuffer b; digitor::CommandEncoder e(b); int value=0; e.dispatch([&]{value=7;}); e.finish(); digitor::Fence f; q.submit(b,&f,2); assert(value==7 && f.value()==2); }
    { digitor::ShaderCompiler c; digitor::ShaderCache cache; auto& s=cache.get_or_compile(c,digitor::ShaderLanguage::glsl,digitor::ShaderStage::compute,"layout(binding=2, local_size_x=8) in; void main(){}"); assert(s.reflection.bindings[0].binding==2 && s.reflection.workgroup_size[0]==8); assert(cache.size()==1); }
    { digitor::RenderGraph g; auto r=g.create_transient(64); int ran=0; g.add_pass({"write",{},{{r,digitor::ResourceState::shader_write}},[&](auto&e){e.dispatch([&]{++ran;});}}); g.add_pass({"read",{{r,digitor::ResourceState::shader_read}},{},[&](auto&e){e.dispatch([&]{++ran;});}}); g.compile(); digitor::CommandQueue q; g.execute(q); assert(ran==2 && g.order().size()==2 && g.barriers().size()==2); }
    { digitor::Color in{.2f,.4f,.6f,1},cpu{},gpu{}; digitor::ColorGrade grade; grade.exposure=1; grade.saturation=.8f; digitor::grade_image_cpu(&in,&cpu,1,grade); digitor::CommandBuffer b; digitor::CommandEncoder e(b); digitor::grade_image_gpu(e,&in,&gpu,1,grade); e.finish(); digitor::CommandQueue q; q.submit(b); assert(std::abs(cpu.r-gpu.r)<1e-6f && std::abs(cpu.g-gpu.g)<1e-6f); }
    test_preferred_backend_selection();
    test_unavailable_backend_fallback();

    { digitor::CpuBackend backend; std::vector<uint8_t> input{1,2,3,4}, output;
      assert(backend.initialize(true));
      assert(backend.render_rgba8(1, 1, input, output) == DIGITOR_RESULT_OK);
      assert(output == input);
      assert(backend.render_rgba8(1, 1, {}, output) == DIGITOR_RESULT_OK);
      assert((output == std::vector<uint8_t>{0,0,0,255})); }

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
    void* mapped = nullptr;
    const auto map_result = digitor_map_buffer(buffer, 16, 32, &mapped);
    assert(map_result == DIGITOR_RESULT_OK);
    assert(mapped != nullptr);
    if (mapped) std::memset(mapped, 0x5a, 32);
    assert(digitor_map_buffer(buffer, 0, 1, &mapped) == DIGITOR_RESULT_INVALID_ARGUMENT);
    assert(digitor_unmap_buffer(buffer) == DIGITOR_RESULT_OK);
    assert(digitor_unmap_buffer(buffer) == DIGITOR_RESULT_INVALID_ARGUMENT);
    assert(digitor_map_buffer(buffer, 4090, 7, &mapped) == DIGITOR_RESULT_INVALID_ARGUMENT);
    assert(digitor_destroy_buffer(buffer) == DIGITOR_RESULT_OK);
    assert(digitor_destroy_buffer(buffer) == DIGITOR_RESULT_INVALID_ARGUMENT);

    DigitorBufferDesc device_buffer_desc{64, DIGITOR_BUFFER_USAGE_STORAGE};
    assert(digitor_create_buffer(context, &device_buffer_desc, &buffer) == DIGITOR_RESULT_OK);
    assert(digitor_map_buffer(buffer, 0, 0, &mapped) == DIGITOR_RESULT_INVALID_ARGUMENT);
    assert(digitor_destroy_buffer(buffer) == DIGITOR_RESULT_OK);

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
