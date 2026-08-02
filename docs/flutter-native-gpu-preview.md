# Native GPU preview presentation status

This change is a **draft foundation**, not Milestone 4 hardware sign-off.

## Verified source trace

The compatibility SDK currently creates a `SharedRenderer` in
`src/ffi/flutter_sdk.cpp`. Its `flutter-preview` graph pass generates a color
gradient in `VideoFrame::pixels`; `digitor_sdk_preview_async` renders it,
converts every float channel with `to_byte`, stages the result in a
`std::vector<uint8_t>`, and `digitor_sdk_get_native_texture` exposes that CPU
pointer. This remains only as explicitly selected CPU compatibility behavior.
Strict mode fails before invoking that renderer.

The production timeline path in `src/render/timeline_render_runtime.cpp` does
validate a final `RenderVideoFrame::gpu` and rejects mixed CPU/GPU storage,
backend/context mismatch, stale contexts, and invalid compositor results.
However, backend execution remains supplied through `TimelineRenderCallbacks`
(`create_gpu_target`, `decode_video`, `apply_effects`, and `composite`). The SDK
session does not construct a production `TimelineRenderRuntime`, and
`TimelineMediaAdapter::deliver_preview` is a callback-only boundary. Therefore
the repository does not yet provide a production binding from the Flutter SDK
session to a final timeline `ProcessedGpuFrame`.

No Windows, Android, macOS, or iOS Flutter plugin/registrar implementation is
present under `flutter/`; it contains only Dart FFI bindings. Consequently the
runtime capability probe reports CPU-fallback-only, and strict mode reports an
explicit unavailable result rather than claiming that an opaque or CPU pointer
is a native texture.

## Added contract

The additive C ABI distinguishes the legacy CPU view from a versioned native
GPU descriptor and exposes explicit preview-mode and capability queries.
`NativePreviewPresentationSession` accepts the exact shared
`ProcessedGpuFrame`, validates readiness, backend, device/context, generation,
display-ready color metadata, pixel format, and protected-content policy, then
hands that same shared owner to a platform-only `NativePreviewTextureHost`.
It retains one displayed and one pending frame, replaces stale pending work,
and waits for the host's consumption notification before retiring the displayed
owner. It never invokes validation readback or accesses pixel storage.

## Platform qualification and remaining work

Platform hosts must still be implemented and qualified against real Flutter
embeddings: D3D11On12/shared-resource interop on Windows, AHardwareBuffer or a
Surface producer on Android, and IOSurface/CVPixelBuffer-backed Flutter textures
on macOS and iOS. Vulkan external-memory/semaphore and GLES shared-context/native
fence paths are also unbound. A GPU display transform is required before this
presenter for scene-linear output; scene-linear submissions currently fail.

There are no real-device results or performance measurements in this Linux
environment. Windows, Vulkan Android, GLES Android, macOS, and physical iOS
qualification remain required. The draft must not be marked complete until
those hosts display real timeline frames and runtime instrumentation confirms
zero CPU fallback frames and zero bytes read back. Direct hardware encoder
submission remains out of scope.
