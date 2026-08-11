#include "core/engine_production_runtime.hpp"

#include "platform/android/android_engine_production.hpp"
#include "platform/apple/apple_engine_production.hpp"
#include "platform/windows/windows_engine_production.hpp"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace digitor {
bool engine_production_runtime_supported_platform() noexcept {
#if defined(_WIN32) || defined(__ANDROID__) || defined(__APPLE__)
  return true;
#else
  return false;
#endif
}

std::unique_ptr<ProductionIntegrationRuntime> install_engine_production_runtime(
    const BackendProductionCapability& capability, std::string* diagnostic) noexcept {
  if (!capability.valid()) {
    if (diagnostic) *diagnostic = "selected backend has no production GPU capability";
    return {};
  }
#if defined(_WIN32)
  return install_windows_engine_production_runtime(capability, diagnostic);
#elif defined(__ANDROID__)
  return install_android_engine_production_runtime(capability, diagnostic);
#elif defined(__APPLE__)
  #if TARGET_OS_IPHONE
  return install_apple_engine_production_runtime(DIGITOR_FLUTTER_PRODUCTION_PLUGIN_IOS, capability, diagnostic);
  #else
  return install_apple_engine_production_runtime(DIGITOR_FLUTTER_PRODUCTION_PLUGIN_MACOS, capability, diagnostic);
  #endif
#else
  if (diagnostic) *diagnostic = "Flutter production runtime is not required on this host";
  return {};
#endif
}
}  // namespace digitor
