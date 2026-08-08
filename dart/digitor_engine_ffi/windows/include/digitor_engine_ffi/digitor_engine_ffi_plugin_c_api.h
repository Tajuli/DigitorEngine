#ifndef FLUTTER_PLUGIN_DIGITOR_ENGINE_FFI_PLUGIN_C_API_H_
#define FLUTTER_PLUGIN_DIGITOR_ENGINE_FFI_PLUGIN_C_API_H_

#include <flutter_plugin_registrar.h>

#ifdef DIGITOR_ENGINE_FFI_PLUGIN_EXPORTS
#define DIGITOR_ENGINE_FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define DIGITOR_ENGINE_FFI_PLUGIN_EXPORT __declspec(dllimport)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

DIGITOR_ENGINE_FFI_PLUGIN_EXPORT void DigitorEngineFfiPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar);

#if defined(__cplusplus)
}  // extern "C"
#endif

#endif  // FLUTTER_PLUGIN_DIGITOR_ENGINE_FFI_PLUGIN_C_API_H_
