#ifndef DIGITOR_PLUGIN_APP_INTEGRATION_C_H
#define DIGITOR_PLUGIN_APP_INTEGRATION_C_H

#include "digitor/digitor.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DigitorPluginAppKindC {
  DIGITOR_PLUGIN_APP_FILTER = 0,
  DIGITOR_PLUGIN_APP_EFFECT = 1,
  DIGITOR_PLUGIN_APP_TRANSITION = 2
} DigitorPluginAppKindC;

typedef enum DigitorPluginAppCompatibilityC {
  DIGITOR_PLUGIN_APP_COMPATIBLE = 0,
  DIGITOR_PLUGIN_APP_ENGINE_TOO_OLD = 1,
  DIGITOR_PLUGIN_APP_BACKEND_UNAVAILABLE = 2,
  DIGITOR_PLUGIN_APP_CAPABILITY_UNAVAILABLE = 3,
  DIGITOR_PLUGIN_APP_INVALID_METADATA = 4
} DigitorPluginAppCompatibilityC;

typedef struct DigitorPluginAppCatalogItemC {
  uint32_t struct_size;
  const char* plugin_id;
  const char* version;
  const char* package_sha256;
  DigitorPluginAppKindC kind;
  const char* localized_name;
  const char* localized_description;
  const char* category;
  const char* thumbnail_url;
  const char* preview_media_url;
  const char* minimum_engine_version;
  uint32_t flags;
} DigitorPluginAppCatalogItemC;

typedef struct DigitorPluginAppInstalledItemC {
  uint32_t struct_size;
  const char* plugin_id;
  const char* version;
  const char* package_sha256;
  const char* install_path;
  DigitorPluginAppKindC kind;
  uint32_t flags;
} DigitorPluginAppInstalledItemC;

typedef struct DigitorPluginAppHostC {
  uint32_t struct_size;
  void* user_data;
  DigitorResult (*catalog_count)(void*, size_t*);
  DigitorResult (*catalog_read)(void*, size_t, const char*, DigitorPluginAppCatalogItemC*);
  DigitorResult (*compatibility)(void*, const DigitorPluginAppCatalogItemC*, DigitorPluginAppCompatibilityC*, char*, size_t);
  DigitorResult (*install)(void*, const char*, const char*, char*, size_t);
  DigitorResult (*update)(void*, const char*, const char*, char*, size_t);
  DigitorResult (*uninstall)(void*, const char*, const char*, char*, size_t);
  DigitorResult (*installed_count)(void*, size_t*);
  DigitorResult (*installed_read)(void*, size_t, DigitorPluginAppInstalledItemC*);
  DigitorResult (*recover_exact)(void*, const char*, const char*, const char*, char*, size_t);
  DigitorResult (*process)(void*, const void*, char*, size_t);
} DigitorPluginAppHostC;

typedef struct DigitorPluginAppIntegrationC DigitorPluginAppIntegrationC;

DIGITOR_API DigitorResult digitor_plugin_app_integration_create(const DigitorPluginAppHostC*, DigitorPluginAppIntegrationC**);
DIGITOR_API void digitor_plugin_app_integration_destroy(DigitorPluginAppIntegrationC*);
DIGITOR_API DigitorResult digitor_plugin_app_catalog_count(DigitorPluginAppIntegrationC*, size_t*);
DIGITOR_API DigitorResult digitor_plugin_app_catalog_read(DigitorPluginAppIntegrationC*, size_t, const char*, DigitorPluginAppCatalogItemC*);
DIGITOR_API DigitorResult digitor_plugin_app_check_compatibility(DigitorPluginAppIntegrationC*, const DigitorPluginAppCatalogItemC*, DigitorPluginAppCompatibilityC*, char*, size_t);
DIGITOR_API DigitorResult digitor_plugin_app_install(DigitorPluginAppIntegrationC*, const char*, const char*, char*, size_t);
DIGITOR_API DigitorResult digitor_plugin_app_update(DigitorPluginAppIntegrationC*, const char*, const char*, char*, size_t);
DIGITOR_API DigitorResult digitor_plugin_app_uninstall(DigitorPluginAppIntegrationC*, const char*, const char*, char*, size_t);
DIGITOR_API DigitorResult digitor_plugin_app_installed_count(DigitorPluginAppIntegrationC*, size_t*);
DIGITOR_API DigitorResult digitor_plugin_app_installed_read(DigitorPluginAppIntegrationC*, size_t, DigitorPluginAppInstalledItemC*);
DIGITOR_API DigitorResult digitor_plugin_app_recover_exact(DigitorPluginAppIntegrationC*, const char*, const char*, const char*, char*, size_t);
DIGITOR_API DigitorResult digitor_plugin_app_process(DigitorPluginAppIntegrationC*, const void*, char*, size_t);
DIGITOR_API uint32_t digitor_plugin_app_integration_c_abi_version(void);

#ifdef __cplusplus
}
#endif
#endif
