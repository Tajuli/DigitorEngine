#pragma once

#include "digitor/digitor.h"
#include "digitor/windows_d3d12_effect_provider.hpp"

#include <memory>
#include <string>

namespace digitor {

struct WindowsD3D12BuiltinEffectShadersResult final {
  WindowsD3D12EffectDispatch dispatch;
  std::shared_ptr<void> lifetime;
  std::string package_identity;
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

// Creates the repository-owned HLSL compute package for all built-in effects.
// `device` must be the exact ID3D12Device used by the Windows native provider.
[[nodiscard]] WindowsD3D12BuiltinEffectShadersResult
create_windows_d3d12_builtin_effect_shaders(void* device) noexcept;

}  // namespace digitor
