#pragma once

#include <stdint.h>
#include "digitor/digitor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ATTACHMENT_VERSION 1u

typedef enum DigitorFlutterProductionPluginPlatform {
  DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS = 1,
  DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ANDROID = 2,
  DIGITOR_FLUTTER_PRODUCTION_PLUGIN_MACOS = 3,
  DIGITOR_FLUTTER_PRODUCTION_PLUGIN_IOS = 4
} DigitorFlutterProductionPluginPlatform;

typedef struct DigitorFlutterProductionPluginAttachment {
  uint32_t struct_size;
  uint32_t api_version;
  uint32_t platform;
  const void* flutter_texture_registrar;
  const char* implementation_identity;
} DigitorFlutterProductionPluginAttachment;

DIGITOR_API DigitorResult digitor_flutter_production_plugin_attach(
    const DigitorFlutterProductionPluginAttachment* attachment);
DIGITOR_API DigitorResult digitor_flutter_production_plugin_detach(
    const void* flutter_texture_registrar);
DIGITOR_API uint8_t digitor_flutter_production_plugin_attached(void);
// Returns a thread-local snapshot of the latest production-plugin bootstrap
// diagnostic. The returned pointer remains valid until the next call on the
// same thread. An empty string means no bootstrap error is currently recorded.
DIGITOR_API const char* digitor_flutter_production_plugin_last_error(void);

#ifdef __cplusplus
}
#endif
