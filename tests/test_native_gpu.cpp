#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
#include "gpu/gpu_backend.hpp"

int main() {
#if defined(_WIN32)
    const std::vector<std::uint8_t> reference{
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255};
    bool exercised = false;
    for (const auto api : {DIGITOR_RENDERER_VULKAN, DIGITOR_RENDERER_D3D12}) {
        auto backend = digitor::create_native_backend(api);
        if (!backend || !backend->initialize(true)) continue;
        std::vector<std::uint8_t> pixels;
        assert(backend->render_rgba8(2, 2, reference, pixels) == DIGITOR_RESULT_OK);
        assert(pixels == reference);
        assert(backend->render_rgba8(2, 2, {}, pixels) == DIGITOR_RESULT_OK);
        assert((pixels == std::vector<std::uint8_t>{0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255}));
        backend->shutdown();
        exercised = true;
    }
    if (!exercised) {
        std::cerr << "SKIP: no native GPU device is available; deterministic core tests still ran.\n";
        return 77;
    }
#else
    std::cout << "Native Windows GPU integration test skipped.\n";
#endif
}
