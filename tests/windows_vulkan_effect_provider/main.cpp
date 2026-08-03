#include "digitor/windows_vulkan_effect_provider.hpp"

#include <iostream>

int main() {
  const auto result = digitor::create_windows_vulkan_effect_provider({});
  if (result.result != DIGITOR_RESULT_INVALID_ARGUMENT) {
    std::cerr << "expected invalid-argument fail-closed result\n";
    return 1;
  }
  std::cout << "WINDOWS_VULKAN_EFFECT_PROVIDER_CONTRACT=PASS\n";
  std::cout << "PHYSICAL_EXECUTION=REQUIRES_WINDOWS_VULKAN_GPU\n";
  return 0;
}
