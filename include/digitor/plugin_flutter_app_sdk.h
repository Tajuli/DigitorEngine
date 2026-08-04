#ifndef DIGITOR_PLUGIN_FLUTTER_APP_SDK_H
#define DIGITOR_PLUGIN_FLUTTER_APP_SDK_H
#include <stddef.h>
#include <stdint.h>
#include "digitor/digitor.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum DigitorPluginAccessDecisionC {
  DIGITOR_PLUGIN_ACCESS_ALLOW_PREVIEW = 0,
  DIGITOR_PLUGIN_ACCESS_ALLOW_EXPORT = 1,
  DIGITOR_PLUGIN_ACCESS_DENY_EXPORT = 2
} DigitorPluginAccessDecisionC;

typedef struct DigitorPluginFlutterHostC {
  size_t struct_size;
  void* user_data;
  DigitorResult (*catalog_json)(void*, const char* locale, char* out_json, size_t capacity, size_t* required);
  DigitorResult (*installed_json)(void*, char* out_json, size_t capacity, size_t* required);
  DigitorResult (*install)(void*, const char* plugin_id, const char* version, char* diagnostic, size_t capacity);
  DigitorResult (*uninstall)(void*, const char* plugin_id, const char* version, char* diagnostic, size_t capacity);
  DigitorResult (*access)(void*, const char* plugin_id, const char* version, uint8_t export_surface, DigitorPluginAccessDecisionC* decision);
  DigitorResult (*process_json)(void*, const char* request_json, char* response_json, size_t capacity, size_t* required);
} DigitorPluginFlutterHostC;

typedef struct DigitorPluginFlutterSdkC DigitorPluginFlutterSdkC;
DIGITOR_API uint32_t digitor_plugin_flutter_sdk_abi_version(void);
DIGITOR_API DigitorResult digitor_plugin_flutter_sdk_create(const DigitorPluginFlutterHostC*, DigitorPluginFlutterSdkC**);
DIGITOR_API void digitor_plugin_flutter_sdk_destroy(DigitorPluginFlutterSdkC*);
DIGITOR_API DigitorResult digitor_plugin_flutter_catalog_json(DigitorPluginFlutterSdkC*, const char*, char*, size_t, size_t*);
DIGITOR_API DigitorResult digitor_plugin_flutter_installed_json(DigitorPluginFlutterSdkC*, char*, size_t, size_t*);
DIGITOR_API DigitorResult digitor_plugin_flutter_install(DigitorPluginFlutterSdkC*, const char*, const char*, char*, size_t);
DIGITOR_API DigitorResult digitor_plugin_flutter_uninstall(DigitorPluginFlutterSdkC*, const char*, const char*, char*, size_t);
DIGITOR_API DigitorResult digitor_plugin_flutter_process_json(DigitorPluginFlutterSdkC*, const char*, uint8_t export_surface, char*, size_t, size_t*);
#ifdef __cplusplus
}
#endif
#endif
