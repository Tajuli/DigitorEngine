#include <stddef.h>
#include <string.h>

#include "digitor/digitor.h"

_Static_assert(sizeof(DigitorResult) == 4, "DigitorResult ABI changed");
_Static_assert(sizeof(DigitorRendererBackend) == 4, "backend enum ABI changed");
_Static_assert(sizeof(DigitorEngineConfig) == 8, "config ABI changed");
_Static_assert(offsetof(DigitorRendererInfo, backend_name) == 4, "info ABI changed");
_Static_assert(sizeof(DigitorRendererInfo) == 200, "info ABI changed");

int main(void) {
    DigitorEngineConfig config = {DIGITOR_RENDERER_CPU, 0, 0};
    DigitorRendererInfo info = {0};
    if (strcmp(digitor_get_version(), "0.2.0") != 0) return 1;
    if (digitor_initialize(&config) != DIGITOR_RESULT_OK) return 2;
    if (digitor_get_renderer_info(&info) != DIGITOR_RESULT_OK) return 3;
    if (info.backend != DIGITOR_RENDERER_CPU || info.is_gpu ||
        !info.supports_compute || info.supports_fp16 || !info.supports_fp32) return 4;
    return digitor_shutdown() == DIGITOR_RESULT_OK ? 0 : 5;
}
