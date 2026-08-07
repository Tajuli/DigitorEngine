# DigitorEngine 5.51 App Integration RC

This release candidate is the supported integration boundary for using DigitorEngine from the Digitor Flutter application before physical-device zero-copy qualification is complete.

## Supported application path

1. Initialize the engine with the platform backend policy.
2. Create a production media/decode session.
3. Construct `UnifiedRealMediaRuntime` with the existing timeline frame resolver, GPU processing callback and platform presenter.
4. Use the runtime for play, pause, stop, seek, scrub, frame stepping, rate control and presentation.
5. Use the existing production export APIs. Software/FFmpeg export is an explicit supported RC path. Hardware-required export must fail closed when no qualified native encoder adapter is registered.

## Backend policy

- Windows: Vulkan, then D3D12, then CPU when fallback is enabled before session creation.
- Android: Vulkan, then OpenGL ES, then CPU when fallback is enabled before session creation.
- macOS and iOS: Metal, then CPU when fallback is enabled before session creation.

A GPU-selected session never silently changes to CPU after creation.

## Preview modes

### Native production preview

Use `UnifiedRealMediaRuntime` with a real `NativeFlutterPresenter`. The presenter receives a `ProcessedGpuFrame` and `UnifiedNativeTextureDescriptor`. The platform plugin owns Flutter texture registration and must validate backend, device/context identity, generation, lifetime and synchronization.

### Explicit compatibility preview

The legacy `digitor_sdk_*` CPU-readable preview API remains available only as an explicitly declared compatibility-only validation path. `DigitorNativeTexture` is CPU memory and must not be registered or described as a zero-copy GPU texture.

`DIGITOR_PREVIEW_MODE_NATIVE_GPU_STRICT` fails with `DIGITOR_RESULT_BACKEND_UNAVAILABLE` until a real native presenter is attached. This is intentional fail-closed behavior.

## Flutter integration rule

Do not call the legacy synthetic preview generator as the final media preview implementation. The app should bind its platform texture host to `UnifiedRealMediaRuntime`; the CPU compatibility API may be used temporarily for UI wiring and diagnostics only.

## Export rule

- Software/FFmpeg export is selected explicitly by the app and reported as software export.
- Hardware-required export is selected explicitly and fails when the platform adapter is unavailable.
- No mid-session GPU-to-CPU fallback is allowed.
- Preview and export must receive the same immutable timeline/node/color revision.

## Release qualification boundary

Version 5.51 is app-integration ready at the source/package contract level. It is not a claim that every Windows, Android, macOS or iOS device has completed zero-copy preview and hardware-encode qualification. Physical-device evidence remains mandatory before those paths are marketed as hardware-qualified.
