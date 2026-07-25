#include <cassert>
#include <cstring>
#include <iostream>

#include "digitor/digitor.h"

int main() {
    assert(std::strcmp(digitor_get_version(), "0.1.0") == 0);

    DigitorEngineConfig config{};
    config.preferred_backend = DIGITOR_RENDERER_AUTO;
    config.enable_validation = 0;
    config.allow_cpu_fallback = 1;

    assert(digitor_initialize(&config) == DIGITOR_RESULT_OK);

    DigitorRendererInfo info{};
    assert(digitor_get_renderer_info(&info) == DIGITOR_RESULT_OK);
    assert(info.backend == DIGITOR_RENDERER_CPU);

    DigitorRenderContext* context = nullptr;
    assert(digitor_create_render_context(&context) == DIGITOR_RESULT_OK);
    assert(context != nullptr);
    assert(digitor_destroy_render_context(context) == DIGITOR_RESULT_OK);

    assert(digitor_shutdown() == DIGITOR_RESULT_OK);

    std::cout << "All DigitorEngine foundation tests passed.\n";
    return 0;
}
