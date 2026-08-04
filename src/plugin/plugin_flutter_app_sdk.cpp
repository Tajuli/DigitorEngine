#include "digitor/plugin_flutter_app_sdk.h"
#include <cstring>
#include <new>
#include <string>

struct DigitorPluginFlutterSdkC { DigitorPluginFlutterHostC host; };
namespace {
DigitorResult copy_text(const std::string& s, char* out, size_t cap, size_t* required) {
  if (!required) return DIGITOR_RESULT_INVALID_ARGUMENT;
  *required = s.size() + 1;
  if (!out || cap < *required) return DIGITOR_RESULT_RESOURCE_IN_USE;
  std::memcpy(out, s.c_str(), *required);
  return DIGITOR_RESULT_OK;
}
bool valid(const DigitorPluginFlutterSdkC* sdk) { return sdk && sdk->host.struct_size >= sizeof(DigitorPluginFlutterHostC); }
}
extern "C" {
uint32_t digitor_plugin_flutter_sdk_abi_version(void) { return 1; }
DigitorResult digitor_plugin_flutter_sdk_create(const DigitorPluginFlutterHostC* host, DigitorPluginFlutterSdkC** out) {
  if (!host || !out || host->struct_size < sizeof(DigitorPluginFlutterHostC) || !host->catalog_json || !host->installed_json || !host->install || !host->uninstall || !host->access || !host->process_json) return DIGITOR_RESULT_INVALID_ARGUMENT;
  try { auto* sdk = new DigitorPluginFlutterSdkC{}; sdk->host = *host; *out = sdk; return DIGITOR_RESULT_OK; }
  catch (const std::bad_alloc&) { *out = nullptr; return DIGITOR_RESULT_OUT_OF_MEMORY; }
  catch (...) { *out = nullptr; return DIGITOR_RESULT_INTERNAL_ERROR; }
}
void digitor_plugin_flutter_sdk_destroy(DigitorPluginFlutterSdkC* sdk) { delete sdk; }
DigitorResult digitor_plugin_flutter_catalog_json(DigitorPluginFlutterSdkC* sdk, const char* locale, char* out, size_t cap, size_t* required) {
  if (!valid(sdk) || !locale || !required) return DIGITOR_RESULT_INVALID_ARGUMENT;
  try { return sdk->host.catalog_json(sdk->host.user_data, locale, out, cap, required); } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}
DigitorResult digitor_plugin_flutter_installed_json(DigitorPluginFlutterSdkC* sdk, char* out, size_t cap, size_t* required) {
  if (!valid(sdk) || !required) return DIGITOR_RESULT_INVALID_ARGUMENT;
  try { return sdk->host.installed_json(sdk->host.user_data, out, cap, required); } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}
DigitorResult digitor_plugin_flutter_install(DigitorPluginFlutterSdkC* sdk, const char* id, const char* version, char* diagnostic, size_t cap) {
  if (!valid(sdk) || !id || !*id || !version || !*version) return DIGITOR_RESULT_INVALID_ARGUMENT;
  try { return sdk->host.install(sdk->host.user_data, id, version, diagnostic, cap); } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}
DigitorResult digitor_plugin_flutter_uninstall(DigitorPluginFlutterSdkC* sdk, const char* id, const char* version, char* diagnostic, size_t cap) {
  if (!valid(sdk) || !id || !*id || !version || !*version) return DIGITOR_RESULT_INVALID_ARGUMENT;
  try { return sdk->host.uninstall(sdk->host.user_data, id, version, diagnostic, cap); } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}
DigitorResult digitor_plugin_flutter_process_json(DigitorPluginFlutterSdkC* sdk, const char* request, uint8_t export_surface, char* out, size_t cap, size_t* required) {
  if (!valid(sdk) || !request || !*request || !required) return DIGITOR_RESULT_INVALID_ARGUMENT;
  try {
    const std::string r(request);
    const auto id_pos = r.find("\"plugin_id\":\""); const auto v_pos = r.find("\"version\":\"");
    if (id_pos == std::string::npos || v_pos == std::string::npos) return DIGITOR_RESULT_INVALID_ARGUMENT;
    const auto id_begin=id_pos+13, id_end=r.find('"',id_begin); const auto v_begin=v_pos+11, v_end=r.find('"',v_begin);
    if (id_end==std::string::npos || v_end==std::string::npos) return DIGITOR_RESULT_INVALID_ARGUMENT;
    const auto id=r.substr(id_begin,id_end-id_begin), version=r.substr(v_begin,v_end-v_begin);
    DigitorPluginAccessDecisionC decision{};
    auto access=sdk->host.access(sdk->host.user_data,id.c_str(),version.c_str(),export_surface,&decision);
    if (access != DIGITOR_RESULT_OK) return access;
    if (export_surface && decision != DIGITOR_PLUGIN_ACCESS_ALLOW_EXPORT) return copy_text("{\"status\":\"export_denied_by_app\"}", out, cap, required);
    return sdk->host.process_json(sdk->host.user_data, request, out, cap, required);
  } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}
}
