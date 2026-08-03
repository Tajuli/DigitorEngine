#include "digitor/apple_metal_effect_provider.hpp"

#if defined(__APPLE__)

#import <Metal/Metal.h>

#include <mutex>
#include <utility>

namespace digitor {
namespace {

MTLPixelFormat metal_effect_format(NativeEffectFormat format) noexcept {
  switch (format) {
    case NativeEffectFormat::rgba8_unorm: return MTLPixelFormatRGBA8Unorm;
    case NativeEffectFormat::bgra8_unorm: return MTLPixelFormatBGRA8Unorm;
    case NativeEffectFormat::rgba16_float: return MTLPixelFormatRGBA16Float;
    default: return MTLPixelFormatInvalid;
  }
}

struct MetalEffectState final {
  id<MTLDevice> device;
  id<MTLCommandQueue> queue;
  id<MTLCommandBuffer> command_buffer;
  std::uint64_t identity{};
  std::string shader_identity;
  AppleMetalEffectDispatch dispatch;
  std::mutex mutex;
  bool recording{};

  bool begin(std::string& diagnostic) {
    if (recording && command_buffer) return true;
    command_buffer = [queue commandBuffer];
    if (!command_buffer) {
      diagnostic = "Metal effect command buffer creation failed";
      return false;
    }
    recording = true;
    return true;
  }

  void abort_recording() noexcept {
    command_buffer = nil;
    recording = false;
  }

  bool submit_and_wait(std::string& diagnostic) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!recording || !command_buffer) {
      diagnostic = "Metal effect submission has no recorded passes";
      return false;
    }
    id<MTLCommandBuffer> submitted = command_buffer;
    command_buffer = nil;
    recording = false;
    [submitted commit];
    [submitted waitUntilCompleted];
    if (submitted.status == MTLCommandBufferStatusError) {
      diagnostic = submitted.error
          ? std::string(submitted.error.localizedDescription.UTF8String)
          : "Metal effect command buffer failed";
      return false;
    }
    return submitted.status == MTLCommandBufferStatusCompleted;
  }
};

}  // namespace

AppleMetalEffectProviderResult create_apple_metal_effect_provider(
    AppleMetalEffectProviderBindings bindings) noexcept {
  AppleMetalEffectProviderResult out{};
  if (!bindings.device || !bindings.command_queue || !bindings.device_identity ||
      bindings.shader_package_identity.empty() || !bindings.dispatch) {
    out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    out.diagnostic = "Metal effect provider bindings are incomplete";
    return out;
  }
  if (!bindings.supports_external_memory ||
      !bindings.supports_external_synchronization) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "Metal effect provider requires zero-copy interop and synchronization";
    return out;
  }

  auto state = std::make_shared<MetalEffectState>();
  state->device = (__bridge id<MTLDevice>)bindings.device;
  state->queue = (__bridge id<MTLCommandQueue>)bindings.command_queue;
  state->identity = bindings.device_identity;
  state->shader_identity = std::move(bindings.shader_package_identity);
  state->dispatch = std::move(bindings.dispatch);
  if (!state->device || !state->queue || state->queue.device != state->device) {
    out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    out.diagnostic = "Metal effect device and command queue identity mismatch";
    return out;
  }

  NativeEffectBackendProvider provider{};
  provider.backend = NativeEffectBackend::metal;
  provider.device_identity = state->identity;
  provider.supports_external_memory = true;
  provider.supports_external_synchronization = true;
  provider.supports_hdr = bindings.supports_hdr;
  provider.pass_count = [](const EffectDescriptor& descriptor,
                           const EffectInstance&, EffectQuality) {
    switch (descriptor.type) {
      case EffectType::blur:
      case EffectType::glow:
      case EffectType::motion_blur:
        return 2u;
      default:
        return 1u;
    }
  };
  provider.allocate_transient = [state](const NativeEffectSurface& prototype,
                                        NativeEffectSurface& output,
                                        std::string& diagnostic) {
    const MTLPixelFormat format = metal_effect_format(prototype.format);
    if (format == MTLPixelFormatInvalid) {
      diagnostic = "unsupported Metal effect transient format";
      return false;
    }
    MTLTextureDescriptor* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                           width:prototype.width
                                                          height:prototype.height
                                                       mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    id<MTLTexture> texture = [state->device newTextureWithDescriptor:descriptor];
    if (!texture) {
      diagnostic = "Metal effect transient texture allocation failed";
      return false;
    }
    output = prototype;
    output.texture_handle = reinterpret_cast<std::uint64_t>((__bridge_retained void*)texture);
    output.device_identity = state->identity;
    output.engine_owned = true;
    output.external_memory = false;
    output.cpu_mappable = false;
    return true;
  };
  provider.release_transient = [](const NativeEffectSurface& surface) {
    if (surface.engine_owned && surface.texture_handle) {
      CFRelease(reinterpret_cast<CFTypeRef>(surface.texture_handle));
    }
  };
  provider.record_pass = [state](const NativeEffectPass& pass,
                                 std::string& diagnostic) {
    std::lock_guard<std::mutex> lock(state->mutex);
    auto input = (__bridge id<MTLTexture>)reinterpret_cast<void*>(pass.input.texture_handle);
    auto output = (__bridge id<MTLTexture>)reinterpret_cast<void*>(pass.output.texture_handle);
    if (!input || !output || input == output ||
        input.device != state->device || output.device != state->device) {
      diagnostic = "invalid Metal effect pass textures";
      state->abort_recording();
      return false;
    }
    if (!state->begin(diagnostic)) return false;
    if (!state->dispatch((__bridge void*)state->command_buffer, pass,
                         (__bridge void*)input, (__bridge void*)output,
                         diagnostic)) {
      state->abort_recording();
      return false;
    }
    return true;
  };
  provider.submit = [state](std::string& diagnostic) {
    return state->submit_and_wait(diagnostic);
  };

  std::string diagnostic;
  if (!validate_native_effect_provider(provider, diagnostic)) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = std::move(diagnostic);
    return out;
  }
  out.provider = std::move(provider);
  out.lifetime = std::move(state);
  out.result = DIGITOR_RESULT_OK;
  return out;
}

}  // namespace digitor

#else

namespace digitor {
AppleMetalEffectProviderResult create_apple_metal_effect_provider(
    AppleMetalEffectProviderBindings) noexcept {
  AppleMetalEffectProviderResult out{};
  out.result = DIGITOR_RESULT_UNSUPPORTED;
  out.diagnostic = "Metal effect provider is only available on Apple platforms";
  return out;
}
}  // namespace digitor

#endif
