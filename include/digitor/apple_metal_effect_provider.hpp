#pragma once

#include "digitor/digitor.h"
#include "digitor/native_effects.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

// Shader-package callback. The provider owns textures, command buffers,
// submission and completion; the callback only records one effect pass.
using AppleMetalEffectDispatch = std::function<bool(
    void* command_buffer,
    const NativeEffectPass& pass,
    void* input_texture,
    void* output_texture,
    std::string& diagnostic)>;

struct AppleMetalEffectProviderBindings final {
  // Native id<MTLDevice> and id<MTLCommandQueue> bridged as void*.
  void* device{};
  void* command_queue{};
  std::uint64_t device_identity{};
  std::string shader_package_identity;
  AppleMetalEffectDispatch dispatch;
  bool supports_hdr{true};
  bool supports_external_memory{true};
  bool supports_external_synchronization{true};
};

struct AppleMetalEffectProviderResult final {
  NativeEffectBackendProvider provider;
  std::shared_ptr<void> lifetime;
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

[[nodiscard]] AppleMetalEffectProviderResult
create_apple_metal_effect_provider(
    AppleMetalEffectProviderBindings bindings) noexcept;

}  // namespace digitor
