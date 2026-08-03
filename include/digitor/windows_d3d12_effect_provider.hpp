#pragma once

#include "digitor/native_effects.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

// Effect-specific shader dispatch supplied by the D3D12 shader package. The
// provider owns command-list/resource/fence mechanics; this callback only binds
// the already-created pipeline/root signature/descriptors and dispatches.
using WindowsD3D12EffectDispatch = std::function<bool(
    void* command_list,
    const NativeEffectPass& pass,
    void* input_resource,
    void* output_resource,
    std::string& diagnostic)>;

struct WindowsD3D12EffectProviderBindings final {
  // Native ID3D12Device* and ID3D12CommandQueue*. They must be the same device
  // identity used by timeline, preview and encoder providers.
  void* device{};
  void* command_queue{};
  std::uint64_t device_identity{};

  // Stable shader-package identity retained in qualification evidence.
  std::string shader_package_identity;

  // Concrete effect shader dispatcher. Resource creation, transitions,
  // submission and fence waiting remain repository-owned.
  WindowsD3D12EffectDispatch dispatch;

  bool supports_hdr{true};
  bool supports_external_memory{true};
  bool supports_external_synchronization{true};
};

struct WindowsD3D12EffectProviderResult final {
  NativeEffectBackendProvider provider;
  std::shared_ptr<void> lifetime;
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

[[nodiscard]] WindowsD3D12EffectProviderResult
create_windows_d3d12_effect_provider(
    WindowsD3D12EffectProviderBindings bindings) noexcept;

}  // namespace digitor
