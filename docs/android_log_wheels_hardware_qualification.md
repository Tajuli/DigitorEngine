# Android Log Wheels Hardware Qualification

The normal GitHub-hosted Android job verifies arm64 compilation and linking.
It does not execute Vulkan or OpenGL ES on a physical Android GPU.

Full v4.9.0 cross-platform completion requires the optional
`Android Vulkan and GLES hardware` job in
`.github/workflows/native-gpu-qualification.yml`.

## Runner requirements

Register a GitHub Actions self-hosted runner with these labels:

```text
self-hosted
linux
arm64
android-gpu
```

The runner host must provide:

- Android SDK and `sdkmanager`
- Android NDK 27.2.12479018
- CMake
- ADB
- A connected arm64-v8a Android device
- USB debugging authorization for the runner account

The connected device must expose the native Vulkan and/or OpenGL ES backend
used by DigitorEngine. The qualification binary must never silently execute the
CPU reference after a GPU backend has been selected.

## Run procedure

1. Open **Actions** in the DigitorEngine repository.
2. Open **Native GPU Qualification**.
3. Select **Run workflow**.
4. Enable `run_android_hardware`.
5. Run the workflow on the release candidate commit.

The job builds `digitor_log_wheels_native_tests` for arm64-v8a, pushes the
binary through ADB, executes it on the connected device, and uploads:

```text
android-log-wheels-hardware.log
android-device.txt
```

## Required evidence

The hardware log must contain passing evidence for every backend available on
the device, including:

- CPU-source native Log Wheels execution
- GPU-source chaining
- `ProcessedGpuFrame` readiness
- direct GPU preview with no normal preview readback
- dedicated validation readback
- numerical agreement with the FP32 CPU reference
- same-backend repeated determinism
- pipeline-cache creation and reuse
- backend-supported failure injection and recovery
- zero CPU Log Wheels invocations during native execution
- zero Log Wheels fallback invocations

For a device that supports both backends, the expected final records include:

```text
LOG_WHEELS_BACKEND_RESULT backend=Vulkan status=PASS
LOG_WHEELS_BACKEND_RESULT backend=OpenGL ES status=PASS
```

If one backend is genuinely unavailable on the device, the log must report it
as unavailable rather than fabricating a pass. Qualification of that backend
must then be performed on another real Android device that supports it.

## Release decision

A green workflow with the Android hardware job skipped proves only Android
compile/link. Full cross-platform v4.9.0 completion requires passing hardware
artifacts tied to the exact release commit SHA.
