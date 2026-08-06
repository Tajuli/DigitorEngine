#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(_MSC_VER)
#include <intrin.h>
#endif
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <immintrin.h>
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace digitor {

enum class CpuSimdLevel : std::uint8_t { scalar, sse2, sse41, avx2, neon };

struct CpuSimdCapabilities {
  bool sse2{};
  bool sse41{};
  bool avx2{};
  bool neon{};
};

inline CpuSimdCapabilities detect_cpu_simd_capabilities() noexcept {
  CpuSimdCapabilities result{};
#if defined(__aarch64__) || defined(_M_ARM64)
  result.neon = true;
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
  result.neon = true;
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  int registers[4]{};
  __cpuid(registers, 1);
  result.sse2 = (registers[3] & (1 << 26)) != 0;
  result.sse41 = (registers[2] & (1 << 19)) != 0;
  const bool osxsave = (registers[2] & (1 << 27)) != 0;
  const bool avx = (registers[2] & (1 << 28)) != 0;
  bool ymm_state = false;
  if (osxsave && avx) ymm_state = (_xgetbv(0) & 0x6) == 0x6;
  __cpuidex(registers, 7, 0);
  result.avx2 = ymm_state && ((registers[1] & (1 << 5)) != 0);
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
  __builtin_cpu_init();
  result.sse2 = __builtin_cpu_supports("sse2");
  result.sse41 = __builtin_cpu_supports("sse4.1");
  result.avx2 = __builtin_cpu_supports("avx2");
#endif
  return result;
}

inline CpuSimdLevel selected_cpu_simd_level() noexcept {
  const auto caps = detect_cpu_simd_capabilities();
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || defined(_M_ARM64)
  if (caps.neon) return CpuSimdLevel::neon;
#endif
#if defined(__AVX2__)
  if (caps.avx2) return CpuSimdLevel::avx2;
#endif
#if defined(__SSE4_1__)
  if (caps.sse41) return CpuSimdLevel::sse41;
#endif
#if defined(__SSE2__) || defined(_M_X64)
  if (caps.sse2) return CpuSimdLevel::sse2;
#endif
  return CpuSimdLevel::scalar;
}

inline const char* cpu_simd_level_name(CpuSimdLevel level) noexcept {
  switch (level) {
    case CpuSimdLevel::avx2: return "AVX2";
    case CpuSimdLevel::sse41: return "SSE4.1";
    case CpuSimdLevel::sse2: return "SSE2";
    case CpuSimdLevel::neon: return "NEON";
    default: return "Scalar";
  }
}

inline void simd_copy_bytes(const std::uint8_t* source, std::uint8_t* destination,
                            std::size_t count) noexcept {
  std::size_t i = 0;
#if defined(__AVX2__)
  if (selected_cpu_simd_level() == CpuSimdLevel::avx2) {
    for (; i + 32 <= count; i += 32) {
      const auto value = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(source + i));
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(destination + i), value);
    }
  }
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
  if (selected_cpu_simd_level() == CpuSimdLevel::neon) {
    for (; i + 16 <= count; i += 16) {
      vst1q_u8(destination + i, vld1q_u8(source + i));
    }
  }
#elif defined(__SSE2__) || defined(_M_X64)
  if (selected_cpu_simd_level() == CpuSimdLevel::sse2 ||
      selected_cpu_simd_level() == CpuSimdLevel::sse41) {
    for (; i + 16 <= count; i += 16) {
      const auto value = _mm_loadu_si128(reinterpret_cast<const __m128i*>(source + i));
      _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + i), value);
    }
  }
#endif
  if (i < count) std::memcpy(destination + i, source + i, count - i);
}

inline void simd_fill_opaque_black_rgba8(std::uint8_t* destination,
                                         std::size_t pixels) noexcept {
  constexpr std::uint32_t black = 0xff000000U;
  std::size_t i = 0;
#if defined(__AVX2__)
  if (selected_cpu_simd_level() == CpuSimdLevel::avx2) {
    const auto value = _mm256_set1_epi32(static_cast<int>(black));
    for (; i + 8 <= pixels; i += 8)
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(destination + i * 4), value);
  }
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
  if (selected_cpu_simd_level() == CpuSimdLevel::neon) {
    const auto value = vdupq_n_u32(black);
    for (; i + 4 <= pixels; i += 4)
      vst1q_u32(reinterpret_cast<std::uint32_t*>(destination + i * 4), value);
  }
#elif defined(__SSE2__) || defined(_M_X64)
  if (selected_cpu_simd_level() == CpuSimdLevel::sse2 ||
      selected_cpu_simd_level() == CpuSimdLevel::sse41) {
    const auto value = _mm_set1_epi32(static_cast<int>(black));
    for (; i + 4 <= pixels; i += 4)
      _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + i * 4), value);
  }
#endif
  auto* words = reinterpret_cast<std::uint32_t*>(destination);
  for (; i < pixels; ++i) words[i] = black;
}

inline void simd_blend_rgba32f(const float* foreground, const float* background,
                               float* output, std::size_t float_count,
                               float opacity) noexcept {
  opacity = std::clamp(opacity, 0.0f, 1.0f);
  const float inverse = 1.0f - opacity;
  std::size_t i = 0;
#if defined(__AVX2__)
  if (selected_cpu_simd_level() == CpuSimdLevel::avx2) {
    const auto a = _mm256_set1_ps(opacity), b = _mm256_set1_ps(inverse);
    for (; i + 8 <= float_count; i += 8) {
      const auto fg = _mm256_loadu_ps(foreground + i);
      const auto bg = _mm256_loadu_ps(background + i);
      _mm256_storeu_ps(output + i, _mm256_add_ps(_mm256_mul_ps(fg, a), _mm256_mul_ps(bg, b)));
    }
  }
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
  if (selected_cpu_simd_level() == CpuSimdLevel::neon) {
    const auto a = vdupq_n_f32(opacity), b = vdupq_n_f32(inverse);
    for (; i + 4 <= float_count; i += 4) {
      const auto fg = vld1q_f32(foreground + i), bg = vld1q_f32(background + i);
      vst1q_f32(output + i, vaddq_f32(vmulq_f32(fg, a), vmulq_f32(bg, b)));
    }
  }
#elif defined(__SSE2__) || defined(_M_X64)
  if (selected_cpu_simd_level() == CpuSimdLevel::sse2 ||
      selected_cpu_simd_level() == CpuSimdLevel::sse41) {
    const auto a = _mm_set1_ps(opacity), b = _mm_set1_ps(inverse);
    for (; i + 4 <= float_count; i += 4) {
      const auto fg = _mm_loadu_ps(foreground + i), bg = _mm_loadu_ps(background + i);
      _mm_storeu_ps(output + i, _mm_add_ps(_mm_mul_ps(fg, a), _mm_mul_ps(bg, b)));
    }
  }
#endif
  for (; i < float_count; ++i) output[i] = foreground[i] * opacity + background[i] * inverse;
}

}  // namespace digitor
