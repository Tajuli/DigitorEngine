#include <iostream>

#include "digitor/digitor.h"

int main() {
    DigitorEngineConfig config{};
    config.preferred_backend = DIGITOR_RENDERER_AUTO;
    config.enable_validation = 1;
    config.allow_cpu_fallback = 1;

    if (digitor_initialize(&config) != DIGITOR_RESULT_OK) {
        std::cerr << "Failed to initialize DigitorEngine.\n";
        return 1;
    }

    DigitorRendererInfo info{};
    if (digitor_get_renderer_info(&info) == DIGITOR_RESULT_OK) {
        std::cout << "DigitorEngine " << digitor_get_version() << '\n';
        std::cout << "Backend: " << info.backend_name << '\n';
        std::cout << "Device: " << info.device_name << '\n';
        std::cout << "GPU: " << (info.is_gpu ? "yes" : "no") << '\n';
    }

    digitor_shutdown();
    return 0;
}
