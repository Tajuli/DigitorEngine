# Production Native GPU Sign-off

A green hosted CI run is not, by itself, a complete production GPU qualification.

## Required real-hardware matrix

- Windows Direct3D 12: real GPU execution and PASS evidence.
- macOS Metal: real GPU execution and PASS evidence.
- Windows Vulkan: real Vulkan-capable GPU execution on a self-hosted Windows runner.
- Android Vulkan: real physical Android device execution.
- Android OpenGL ES: real physical Android device execution.

## Rejected evidence

The production gate rejects all of the following:

- compile-only success;
- exit code 77 / hardware unavailable;
- skipped jobs;
- Android emulator execution;
- missing backend PASS markers;
- CPU fallback or CPU color-reference invocation;
- intermediate preview readback/re-upload;
- missing numerical parity, cache reuse, native consumer, or device-retirement evidence.

## Workflows

`.github/workflows/native-gpu-qualification.yml` is the regular hosted CI workflow. It truthfully records D3D12 and Metal real execution where available, reports Vulkan opportunistically, and labels Android as compile/link only.

`.github/workflows/production-native-gpu-signoff.yml` is the strict release gate. It requires repository-owned self-hosted runners labelled `digitor-vulkan-gpu` and `android-device`. The final `Production GPU matrix complete` job can pass only after every required real-hardware job succeeds.

No backend may be called production-qualified from compile-only, unavailable, skipped, or simulated evidence.
