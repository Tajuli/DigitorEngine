#pragma once

#include "digitor/digitor.h"
#include "digitor/native_effects.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace digitor {

struct AndroidGlesEffectProviderBindings final {
  // EGLDisplay/EGLContext bridged as void*. The context must be current on the
  // calling thread whenever the provider is created or executed.
  void* egl_display{};
  void* egl_context{};
  std::uint64_t device_identity{};
  bool supports_rgba16f{};
  bool supports_external_textures{true};
  bool supports_external_synchronization{true};
};

struct AndroidGlesEffectProviderResult final {
  NativeEffectBackendProvider provider;
  std::shared_ptr<void> lifetime;
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

// External texture handles are GLuint 2D textures in the exact EGL share
// group. Input/output textures must be distinct and remain alive until submit.
[[nodiscard]] AndroidGlesEffectProviderResult
create_android_gles_effect_provider(
    AndroidGlesEffectProviderBindings bindings) noexcept;

}  // namespace digitor
