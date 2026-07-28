# DigitorEngine v4.7.0 code review and hardening

This package was reviewed from source code rather than README claims.

## Fixed

- Hardened every public core C ABI entry point against C++ exceptions.
- Serialized opaque handle validation and use to prevent stale-handle/use-after-free races.
- Ensured output pointers and structs are cleared on failure paths.
- Added correct allocation cleanup for context, texture, buffer and sampler wrappers.
- Hardened Flutter SDK session validation and stale-session rejection.
- Fixed Flutter SDK worker creation failures leaving `busy` permanently set.
- Fixed unsynchronized seek-position writes.
- Prevented destroying an SDK session from its own callback worker thread.
- Added differentiated out-of-memory versus internal-error reporting in async workers.
- Added render and export dimension overflow checks.
- Added export frame-range overflow and negative-first-frame checks.

## Locally verified

- GCC 14 Debug static build with `-Wall -Wextra -Wpedantic -Werror`.
- GCC 14 Release shared-library build with warnings as errors.
- All available CTest tests passed in static, shared and sanitizer configurations.
- AddressSanitizer and UndefinedBehaviorSanitizer passed with leak detection enabled.
- Installed-package C and C++ consumer projects built and passed.
- Optional FFmpeg mode configured correctly; FFmpeg development libraries were unavailable in this environment, so the intended unavailable fallback path was tested.

## Not hardware-verified here

- Windows D3D12 runtime execution.
- Android OpenGL ES runtime execution.
- Apple Metal runtime execution.
- Vulkan physical-GPU execution.
- FFmpeg-backed encode/decode because development headers/libraries were unavailable.

The source contains CI workflows intended to validate platform-specific compilation and native GPU qualification on suitable runners/hardware.
