#include "digitor/apple_metal_builtin_effect_shaders.hpp"
#include "digitor/apple_metal_effect_provider.hpp"
#include "digitor/effect_system.hpp"
#include "digitor/native_effects.hpp"

#if defined(__APPLE__)
#import <Metal/Metal.h>

#include <cstdint>
#include <iostream>
#include <string>

namespace {

int fail(const std::string& diagnostic) {
  std::cerr << "APPLE_METAL_EFFECTS_QUALIFICATION=FAIL\n";
  std::cerr << "DIAGNOSTIC=" << diagnostic << '\n';
  return 1;
}

id<MTLTexture> make_texture(id<MTLDevice> device, MTLPixelFormat format,
                            NSUInteger width, NSUInteger height) {
  MTLTextureDescriptor* descriptor =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                         width:width
                                                        height:height
                                                     mipmapped:NO];
  descriptor.storageMode = MTLStorageModePrivate;
  descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
  return [device newTextureWithDescriptor:descriptor];
}

digitor::NativeEffectSurface surface(id<MTLTexture> texture,
                                     std::uint64_t device_identity,
                                     digitor::NativeEffectFormat format) {
  digitor::NativeEffectSurface out{};
  out.texture_handle = reinterpret_cast<std::uint64_t>((__bridge void*)texture);
  out.device_identity = device_identity;
  out.width = static_cast<std::uint32_t>(texture.width);
  out.height = static_cast<std::uint32_t>(texture.height);
  out.format = format;
  out.engine_owned = false;
  out.external_memory = true;
  out.cpu_mappable = false;
  return out;
}

bool run_all(digitor::NativeEffectRuntime& runtime,
             const digitor::EffectRegistry& registry,
             id<MTLDevice> device,
             std::uint64_t identity,
             MTLPixelFormat pixel_format,
             digitor::NativeEffectFormat native_format,
             std::string& diagnostic) {
  constexpr NSUInteger width = 64;
  constexpr NSUInteger height = 48;
  for (const auto& descriptor : registry.effects()) {
    id<MTLTexture> input = make_texture(device, pixel_format, width, height);
    id<MTLTexture> output = make_texture(device, pixel_format, width, height);
    if (!input || !output) {
      diagnostic = descriptor.id + ": texture allocation failed";
      return false;
    }

    digitor::EffectInstance instance{};
    instance.effect_id = descriptor.id;
    instance.amount = descriptor.default_amount;
    instance.radius = descriptor.default_radius;
    instance.angle = descriptor.default_angle;
    instance.seed = 0x123456789abcdef0ULL;
    digitor::EffectStack stack;
    if (!stack.add(instance)) {
      diagnostic = descriptor.id + ": valid effect instance was rejected";
      return false;
    }

    std::string local;
    if (!runtime.execute(registry, stack,
                         digitor::EffectQuality::export_quality,
                         surface(input, identity, native_format),
                         surface(output, identity, native_format), &local)) {
      diagnostic = descriptor.id + ": " + local;
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) return fail("MTLCreateSystemDefaultDevice returned nil");
    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (!queue) return fail("Metal command queue creation failed");

    auto shaders = digitor::create_apple_metal_builtin_effect_shaders(
        (__bridge void*)device);
    if (!shaders) return fail("shader package: " + shaders.diagnostic);

    const std::uint64_t identity =
        reinterpret_cast<std::uint64_t>((__bridge void*)device);
    digitor::AppleMetalEffectProviderBindings bindings{};
    bindings.device = (__bridge void*)device;
    bindings.command_queue = (__bridge void*)queue;
    bindings.device_identity = identity;
    bindings.shader_package_identity = shaders.package_identity;
    bindings.dispatch = shaders.dispatch;
    bindings.supports_hdr = true;
    bindings.supports_external_memory = true;
    bindings.supports_external_synchronization = true;

    auto provider = digitor::create_apple_metal_effect_provider(
        std::move(bindings));
    if (!provider) return fail("native provider: " + provider.diagnostic);

    try {
      digitor::NativeEffectRuntime runtime(provider.provider);
      const digitor::EffectRegistry registry;
      if (registry.effects().size() != 9)
        return fail("built-in effect registry count mismatch");

      std::string diagnostic;
      if (!run_all(runtime, registry, device, identity,
                   MTLPixelFormatRGBA8Unorm,
                   digitor::NativeEffectFormat::rgba8_unorm, diagnostic)) {
        return fail("SDR execution: " + diagnostic);
      }
      if (!run_all(runtime, registry, device, identity,
                   MTLPixelFormatRGBA16Float,
                   digitor::NativeEffectFormat::rgba16_float, diagnostic)) {
        return fail("HDR execution: " + diagnostic);
      }

      const auto telemetry = runtime.telemetry();
      if (telemetry.submitted_passes < 18)
        return fail("submitted pass count is too small");
      if (telemetry.cpu_readbacks != 0)
        return fail("CPU readback telemetry is non-zero");
      if (telemetry.cpu_reuploads != 0)
        return fail("CPU re-upload telemetry is non-zero");
      if (telemetry.fallback_dispatches != 0)
        return fail("fallback dispatch telemetry is non-zero");

      std::cout << "APPLE_METAL_EFFECTS_QUALIFICATION=PASS\n";
      std::cout << "DEVICE_NAME=" << device.name.UTF8String << '\n';
      std::cout << "BUILTIN_EFFECTS=" << registry.effects().size() << '\n';
      std::cout << "SUBMITTED_PASSES=" << telemetry.submitted_passes << '\n';
      return 0;
    } catch (const std::exception& error) {
      return fail(std::string("exception: ") + error.what());
    }
  }
}

#else
int main() { return 0; }
#endif
