#include "digitor/plugin_app_integration_c.h"

#include <algorithm>
#include <cstring>
#include <new>

struct DigitorPluginAppIntegrationC { DigitorPluginAppHostC host{}; };

namespace {
void diagnostic(char* out, size_t cap, const char* text) noexcept {
  if (!out || cap == 0) return;
  const char* value = text ? text : "";
  const size_t n = std::min(cap - 1, std::strlen(value));
  std::memcpy(out, value, n);
  out[n] = '\0';
}
bool text(const char* value) noexcept { return value && value[0] != '\0'; }
bool hash(const char* value) noexcept {
  if (!text(value) || std::strlen(value) != 64) return false;
  for (const char* p = value; *p; ++p)
    if (!( (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) return false;
  return true;
}
bool valid_catalog(const DigitorPluginAppCatalogItemC* item) noexcept {
  return item && item->struct_size >= sizeof(*item) && text(item->plugin_id) &&
      text(item->version) && hash(item->package_sha256) && text(item->localized_name) &&
      text(item->category) && text(item->minimum_engine_version) &&
      item->kind <= DIGITOR_PLUGIN_APP_TRANSITION;
}
bool valid_installed(const DigitorPluginAppInstalledItemC* item) noexcept {
  return item && item->struct_size >= sizeof(*item) && text(item->plugin_id) &&
      text(item->version) && hash(item->package_sha256) && text(item->install_path) &&
      item->kind <= DIGITOR_PLUGIN_APP_TRANSITION;
}
template <typename F>
DigitorResult guarded(F&& f, char* out = nullptr, size_t cap = 0) noexcept {
  try { return f(); }
  catch (...) { diagnostic(out, cap, "plugin app host callback raised an exception"); return DIGITOR_RESULT_INTERNAL_ERROR; }
}
}

extern "C" DigitorResult digitor_plugin_app_integration_create(
    const DigitorPluginAppHostC* host, DigitorPluginAppIntegrationC** out) {
  if (out) *out = nullptr;
  if (!host || !out || host->struct_size < sizeof(*host) || !host->catalog_count ||
      !host->catalog_read || !host->compatibility || !host->install || !host->update ||
      !host->uninstall || !host->installed_count || !host->installed_read ||
      !host->recover_exact || !host->process) return DIGITOR_RESULT_INVALID_ARGUMENT;
  try { auto* value = new DigitorPluginAppIntegrationC{}; value->host = *host; *out = value; return DIGITOR_RESULT_OK; }
  catch (const std::bad_alloc&) { return DIGITOR_RESULT_OUT_OF_MEMORY; }
  catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}
extern "C" void digitor_plugin_app_integration_destroy(DigitorPluginAppIntegrationC* value) { delete value; }
extern "C" DigitorResult digitor_plugin_app_catalog_count(DigitorPluginAppIntegrationC* value, size_t* count) {
  if (count) *count = 0;
  if (!value || !count) return DIGITOR_RESULT_INVALID_ARGUMENT;
  return guarded([&]{ return value->host.catalog_count(value->host.user_data, count); });
}
extern "C" DigitorResult digitor_plugin_app_catalog_read(DigitorPluginAppIntegrationC* value, size_t index, const char* locale, DigitorPluginAppCatalogItemC* item) {
  if (!value || !item || item->struct_size < sizeof(*item) || !text(locale)) return DIGITOR_RESULT_INVALID_ARGUMENT;
  const auto result = guarded([&]{ return value->host.catalog_read(value->host.user_data, index, locale, item); });
  return result == DIGITOR_RESULT_OK && !valid_catalog(item) ? DIGITOR_RESULT_INVALID_ARGUMENT : result;
}
extern "C" DigitorResult digitor_plugin_app_check_compatibility(DigitorPluginAppIntegrationC* value, const DigitorPluginAppCatalogItemC* item, DigitorPluginAppCompatibilityC* status, char* out, size_t cap) {
  diagnostic(out, cap, "");
  if (status) *status = DIGITOR_PLUGIN_APP_INVALID_METADATA;
  if (!value || !status || !valid_catalog(item)) return DIGITOR_RESULT_INVALID_ARGUMENT;
  return guarded([&]{ return value->host.compatibility(value->host.user_data, item, status, out, cap); }, out, cap);
}
static DigitorResult mutate(DigitorPluginAppIntegrationC* value, const char* id, const char* version, char* out, size_t cap, int op) {
  diagnostic(out, cap, ""); if (!value || !text(id) || !text(version)) return DIGITOR_RESULT_INVALID_ARGUMENT;
  return guarded([&]{ return op == 0 ? value->host.install(value->host.user_data,id,version,out,cap) : op == 1 ? value->host.update(value->host.user_data,id,version,out,cap) : value->host.uninstall(value->host.user_data,id,version,out,cap); }, out, cap);
}
extern "C" DigitorResult digitor_plugin_app_install(DigitorPluginAppIntegrationC* v,const char* i,const char* ver,char* d,size_t c){return mutate(v,i,ver,d,c,0);} 
extern "C" DigitorResult digitor_plugin_app_update(DigitorPluginAppIntegrationC* v,const char* i,const char* ver,char* d,size_t c){return mutate(v,i,ver,d,c,1);} 
extern "C" DigitorResult digitor_plugin_app_uninstall(DigitorPluginAppIntegrationC* v,const char* i,const char* ver,char* d,size_t c){return mutate(v,i,ver,d,c,2);} 
extern "C" DigitorResult digitor_plugin_app_installed_count(DigitorPluginAppIntegrationC* value,size_t* count){ if(count)*count=0; if(!value||!count)return DIGITOR_RESULT_INVALID_ARGUMENT; return guarded([&]{return value->host.installed_count(value->host.user_data,count);}); }
extern "C" DigitorResult digitor_plugin_app_installed_read(DigitorPluginAppIntegrationC* value,size_t index,DigitorPluginAppInstalledItemC* item){ if(!value||!item||item->struct_size<sizeof(*item))return DIGITOR_RESULT_INVALID_ARGUMENT; auto r=guarded([&]{return value->host.installed_read(value->host.user_data,index,item);}); return r==DIGITOR_RESULT_OK&&!valid_installed(item)?DIGITOR_RESULT_INVALID_ARGUMENT:r; }
extern "C" DigitorResult digitor_plugin_app_recover_exact(DigitorPluginAppIntegrationC* value,const char* id,const char* version,const char* sha,char* out,size_t cap){ diagnostic(out,cap,""); if(!value||!text(id)||!text(version)||!hash(sha))return DIGITOR_RESULT_INVALID_ARGUMENT; return guarded([&]{return value->host.recover_exact(value->host.user_data,id,version,sha,out,cap);},out,cap); }
extern "C" DigitorResult digitor_plugin_app_process(DigitorPluginAppIntegrationC* value,const void* request,char* out,size_t cap){ diagnostic(out,cap,""); if(!value||!request)return DIGITOR_RESULT_INVALID_ARGUMENT; return guarded([&]{return value->host.process(value->host.user_data,request,out,cap);},out,cap); }
extern "C" uint32_t digitor_plugin_app_integration_c_abi_version(void){return 1u;}
