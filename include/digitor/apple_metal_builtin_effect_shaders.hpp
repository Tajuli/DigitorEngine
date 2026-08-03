#pragma once

#include "digitor/apple_metal_effect_provider.hpp"
#include "digitor/digitor.h"

#include <memory>
#include <string>

namespace digitor {

struct AppleMetalBuiltinEffectShadersResult final {
  AppleMetalEffectDispatch dispatch;
  std::shared_ptr<void> lifetime;
  std::string package_identity;
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

// Creates the repository-owned MSL compute package for all built-in effects.
// `device` must be the exact id<MTLDevice> used by the native effects provider.
[[nodiscard]] AppleMetalBuiltinEffectShadersResult
create_apple_metal_builtin_effect_shaders(void* device) noexcept;

}  // namespace digitor
