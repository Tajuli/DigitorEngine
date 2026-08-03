#include "digitor/android_vulkan_effect_provider.hpp"

#include <iostream>

int main() {
  digitor::AndroidVulkanEffectProviderBindings bindings{};
  const auto result = digitor::create_android_vulkan_effect_provider(bindings);
#if defined(__ANDROID__)
  if (result || result.result == DIGITOR_RESULT_OK) {
    std::cerr << "empty Android Vulkan bindings were accepted\n";
    return 1;
  }
#else
  if (result || result.result != DIGITOR_RESULT_UNSUPPORTED) {
    std::cerr << "non-Android provider did not fail as unsupported\n";
    return 1;
  }
#endif
  return 0;
}
