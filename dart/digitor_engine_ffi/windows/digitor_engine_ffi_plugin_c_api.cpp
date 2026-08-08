#include "include/digitor_engine_ffi/digitor_engine_ffi_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "digitor_engine_ffi_plugin.h"

void DigitorEngineFfiPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  digitor_engine_ffi::DigitorEngineFfiPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
