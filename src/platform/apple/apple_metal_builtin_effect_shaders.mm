#include "digitor/apple_metal_builtin_effect_shaders.hpp"

#if defined(__APPLE__)

#import <Metal/Metal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace digitor {
namespace {

constexpr const char* kMetalSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct EffectConstants {
  uint effect_kind;
  uint pass_index;
  uint pass_count;
  uint quality;
  float amount;
  float radius;
  float angle;
  float reserved;
  ulong seed;
};

inline float4 sample_clamped(texture2d<float, access::read> input,
                             int2 p, uint2 size) {
  int2 hi = int2(size) - int2(1);
  return input.read(uint2(clamp(p, int2(0), hi)));
}

inline float hash21(float2 p, ulong seed) {
  float seed_term = float(seed & 0xfffful) * 0.00006103515625;
  return fract(sin(dot(p, float2(127.1, 311.7)) + seed_term) * 43758.5453);
}

kernel void digitor_builtin_effect(
    texture2d<float, access::read> input [[texture(0)]],
    texture2d<float, access::write> output [[texture(1)]],
    constant EffectConstants& c [[buffer(0)]],
    uint2 gid [[thread_position_in_grid]]) {
  uint2 size(output.get_width(), output.get_height());
  if (gid.x >= size.x || gid.y >= size.y) return;

  int2 p = int2(gid);
  float4 src = input.read(gid);
  float3 rgb = src.rgb;
  float amount = clamp(c.amount, 0.0f, 1.0f);
  float radius = clamp(c.radius, 0.0f, 64.0f);
  float2 uv = (float2(gid) + 0.5f) / float2(size);
  float2 centered = uv * 2.0f - 1.0f;

  // 0 blur, 1 sharpen, 2 glow, 3 lens distortion, 4 noise,
  // 5 film grain, 6 chromatic aberration, 7 vignette, 8 motion blur.
  if (c.effect_kind == 0u || c.effect_kind == 2u || c.effect_kind == 8u) {
    int step_size = max(1, int(round(radius * (c.quality == 2u ? 0.5f : 0.25f))));
    int2 axis = int2(step_size, 0);
    if (c.effect_kind == 8u) {
      float radians = c.angle * 0.01745329252f;
      axis = int2(round(cos(radians) * step_size), round(sin(radians) * step_size));
      if (axis.x == 0 && axis.y == 0) axis.x = 1;
    } else if (c.pass_index > 0u) {
      axis = int2(0, step_size);
    }
    float4 a = sample_clamped(input, p - axis * 2, size);
    float4 b = sample_clamped(input, p - axis, size);
    float4 d = sample_clamped(input, p + axis, size);
    float4 e = sample_clamped(input, p + axis * 2, size);
    float3 blurred = (a.rgb + b.rgb * 2.0f + src.rgb * 4.0f + d.rgb * 2.0f + e.rgb) / 10.0f;
    if (c.effect_kind == 2u) {
      float3 highlights = max(blurred - 0.55f, 0.0f) * 2.2222222f;
      rgb = mix(src.rgb, src.rgb + highlights, amount);
    } else {
      rgb = mix(src.rgb, blurred, amount);
    }
  } else if (c.effect_kind == 1u) {
    float3 n = sample_clamped(input, p + int2(0, -1), size).rgb;
    float3 s = sample_clamped(input, p + int2(0, 1), size).rgb;
    float3 w = sample_clamped(input, p + int2(-1, 0), size).rgb;
    float3 e = sample_clamped(input, p + int2(1, 0), size).rgb;
    float3 sharp = src.rgb * 5.0f - n - s - w - e;
    rgb = mix(src.rgb, sharp, amount);
  } else if (c.effect_kind == 3u) {
    float r2 = dot(centered, centered);
    float scale = 1.0f + (amount * 0.35f) * r2;
    float2 warped = centered / scale;
    float2 sample_uv = clamp((warped + 1.0f) * 0.5f, 0.0f, 1.0f);
    uint2 q = min(uint2(sample_uv * float2(size)), size - 1u);
    rgb = input.read(q).rgb;
  } else if (c.effect_kind == 4u || c.effect_kind == 5u) {
    float grain = hash21(float2(gid), c.seed) - 0.5f;
    if (c.effect_kind == 5u) {
      float luminance = dot(src.rgb, float3(0.2126f, 0.7152f, 0.0722f));
      grain *= 0.35f + 0.65f * (1.0f - abs(luminance * 2.0f - 1.0f));
    }
    rgb = src.rgb + grain * amount * (c.effect_kind == 5u ? 0.16f : 0.25f);
  } else if (c.effect_kind == 6u) {
    int offset = max(1, int(round(1.0f + radius * 0.12f)));
    float red = sample_clamped(input, p + int2(offset, 0), size).r;
    float blue = sample_clamped(input, p - int2(offset, 0), size).b;
    rgb = mix(src.rgb, float3(red, src.g, blue), amount);
  } else if (c.effect_kind == 7u) {
    float dist = length(centered * float2(0.9f, 1.0f));
    float vignette = smoothstep(0.45f, 1.15f, dist);
    rgb = src.rgb * (1.0f - vignette * amount * 0.85f);
  }

  output.write(float4(max(rgb, 0.0f), src.a), gid);
}
)MSL";

struct EffectConstants final {
  std::uint32_t effect_kind{};
  std::uint32_t pass_index{};
  std::uint32_t pass_count{};
  std::uint32_t quality{};
  float amount{};
  float radius{};
  float angle{};
  float reserved{};
  std::uint64_t seed{};
};

struct MetalShaderState final {
  id<MTLDevice> device;
  id<MTLLibrary> library;
  id<MTLComputePipelineState> pipeline;
};

bool effect_kind(const std::string& id, std::uint32_t& value) noexcept {
  if (id == "effect.gaussian_blur") value = 0;
  else if (id == "effect.sharpen") value = 1;
  else if (id == "effect.glow") value = 2;
  else if (id == "effect.lens_distortion") value = 3;
  else if (id == "effect.noise") value = 4;
  else if (id == "effect.film_grain") value = 5;
  else if (id == "effect.chromatic_aberration") value = 6;
  else if (id == "effect.vignette") value = 7;
  else if (id == "effect.motion_blur") value = 8;
  else return false;
  return true;
}

std::string ns_error(NSError* error, const char* fallback) {
  if (!error || !error.localizedDescription) return fallback;
  return std::string(error.localizedDescription.UTF8String ?: fallback);
}

}  // namespace

AppleMetalBuiltinEffectShadersResult
create_apple_metal_builtin_effect_shaders(void* device_handle) noexcept {
  AppleMetalBuiltinEffectShadersResult out{};
  @autoreleasepool {
    id<MTLDevice> device = (__bridge id<MTLDevice>)device_handle;
    if (!device) {
      out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
      out.diagnostic = "Metal built-in effect shader package requires a real MTLDevice";
      return out;
    }

    NSError* error = nil;
    NSString* source = [NSString stringWithUTF8String:kMetalSource];
    MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
    options.fastMathEnabled = NO;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:options error:&error];
    if (!library) {
      out.result = DIGITOR_RESULT_SHADER_COMPILE_FAILED;
      out.diagnostic = ns_error(error, "Metal built-in effect library compilation failed");
      return out;
    }
    id<MTLFunction> function = [library newFunctionWithName:@"digitor_builtin_effect"];
    if (!function) {
      out.result = DIGITOR_RESULT_SHADER_COMPILE_FAILED;
      out.diagnostic = "Metal built-in effect kernel was not found";
      return out;
    }
    id<MTLComputePipelineState> pipeline =
        [device newComputePipelineStateWithFunction:function error:&error];
    if (!pipeline) {
      out.result = DIGITOR_RESULT_PIPELINE_CREATION_FAILED;
      out.diagnostic = ns_error(error, "Metal built-in effect pipeline creation failed");
      return out;
    }

    auto state = std::make_shared<MetalShaderState>();
    state->device = device;
    state->library = library;
    state->pipeline = pipeline;

    out.dispatch = [state](void* command_buffer_handle,
                           const NativeEffectPass& pass,
                           void* input_texture_handle,
                           void* output_texture_handle,
                           std::string& diagnostic) {
      id<MTLCommandBuffer> command_buffer =
          (__bridge id<MTLCommandBuffer>)command_buffer_handle;
      id<MTLTexture> input = (__bridge id<MTLTexture>)input_texture_handle;
      id<MTLTexture> output = (__bridge id<MTLTexture>)output_texture_handle;
      if (!command_buffer || !input || !output || input == output ||
          input.device != state->device || output.device != state->device ||
          input.width != output.width || input.height != output.height ||
          input.pixelFormat != output.pixelFormat) {
        diagnostic = "Metal built-in effect dispatch received incompatible resources";
        return false;
      }

      std::uint32_t kind = 0;
      if (!effect_kind(pass.effect.effect_id, kind)) {
        diagnostic = "unknown Metal built-in effect id: " + pass.effect.effect_id;
        return false;
      }

      EffectConstants constants{};
      constants.effect_kind = kind;
      constants.pass_index = pass.pass_index;
      constants.pass_count = pass.pass_count;
      constants.quality = static_cast<std::uint32_t>(pass.quality);
      constants.amount = pass.effect.amount;
      constants.radius = pass.effect.radius;
      constants.angle = pass.effect.angle;
      constants.seed = pass.effect.seed;

      id<MTLComputeCommandEncoder> encoder = [command_buffer computeCommandEncoder];
      if (!encoder) {
        diagnostic = "Metal built-in effect compute encoder creation failed";
        return false;
      }
      [encoder setComputePipelineState:state->pipeline];
      [encoder setTexture:input atIndex:0];
      [encoder setTexture:output atIndex:1];
      [encoder setBytes:&constants length:sizeof(constants) atIndex:0];

      const NSUInteger width = std::max<NSUInteger>(1, state->pipeline.threadExecutionWidth);
      const NSUInteger height = std::max<NSUInteger>(
          1, state->pipeline.maxTotalThreadsPerThreadgroup / width);
      const MTLSize threads_per_group = MTLSizeMake(width, height, 1);
      const MTLSize groups = MTLSizeMake(
          (output.width + width - 1) / width,
          (output.height + height - 1) / height, 1);
      [encoder dispatchThreadgroups:groups threadsPerThreadgroup:threads_per_group];
      [encoder endEncoding];
      diagnostic.clear();
      return true;
    };
    out.lifetime = state;
    out.package_identity = "digitor.apple.metal.builtin-effects.v1";
    out.result = DIGITOR_RESULT_OK;
    return out;
  }
}

}  // namespace digitor

#else

namespace digitor {
AppleMetalBuiltinEffectShadersResult
create_apple_metal_builtin_effect_shaders(void*) noexcept {
  AppleMetalBuiltinEffectShadersResult out{};
  out.result = DIGITOR_RESULT_UNSUPPORTED;
  out.diagnostic = "Metal built-in effect shaders are only available on Apple platforms";
  return out;
}
}  // namespace digitor

#endif
