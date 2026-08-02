# Native platform release completion bundle

This bundle is the authoritative boundary between DigitorEngine's completed cross-platform contracts and the platform-owned native implementations required for a production release.

## One provider per shipped platform

A release package must compile exactly one `NativePlatformProvider` for each platform:

- Windows: D3D12/Vulkan timeline execution, Flutter Windows external texture registration, NVENC/Media Foundation/QSV encoder creation, and Vulkan/DXGI external-memory and semaphore interop.
- Android: Vulkan/GLES timeline execution, Flutter Android texture/surface registration, AHardwareBuffer/EGL synchronization, MediaCodec input-surface encoder and MP4 finalization.
- macOS: Metal timeline execution, Flutter macOS texture registration through IOSurface/CVPixelBuffer, VideoToolbox encoder and MOV/MP4 finalization.
- iOS: Metal timeline execution, Flutter iOS texture registration through CVPixelBuffer/IOSurface, VideoToolbox encoder and lifecycle-safe finalization.

Callback-only, compile-only and simulator providers are not release implementations.

## Provider evidence

Each provider must report separate production identities for:

1. timeline/native compositor implementation;
2. Flutter texture registrar implementation;
3. native hardware encoder implementation.

Every component must bind native APIs, synchronization and zero-copy telemetry. The provider package and build identities must be retained in the release evidence.

## Strict release build

Configure a platform release with:

```text
-DDIGITOR_REQUIRE_NATIVE_PLATFORM_PROVIDER=ON
-DDIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE=/absolute/path/to/provider_source.cpp
-DDIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY=<stable-package-and-build-id>
```

Configuration must fail when the provider source or identity is absent. The provider source is compiled into the engine target; it is not loaded as an unverified runtime callback.

## Completion order

1. Add the four repository-owned provider sources and their platform SDK dependencies.
2. Enable the strict provider build in each platform CI job.
3. Run public C/C++ ABI and installed-consumer tests against the exact release artifacts.
4. Generate `SourceReleaseReadiness` from provider and CI evidence.
5. Run the existing M6 physical-device output, parity, synchronization, long-run and zero-readback matrix.

The engine may be tagged only when both source-readiness and M6 physical-device validators pass for the exact release commit.
