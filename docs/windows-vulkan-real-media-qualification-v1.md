# Windows Vulkan and real-media qualification v1

This gate completes the remaining Windows qualification after D3D12 hardware qualification.

## Vulkan PASS criteria

- `vulkaninfo --summary` resolves a physical Windows GPU implementation.
- Software implementations such as llvmpipe, SwiftShader, Microsoft Basic Render, or CPU Vulkan are rejected.
- `digitor_native_gpu_tests.exe` executes the Vulkan backend.
- Vulkan numerical qualification, failure injection, cache reuse, preview-consumer execution, context retirement, and recovery all pass.
- No Vulkan qualification skip or `status=FAIL` marker is allowed.

## Real-media PASS criteria

- FFmpeg and ffprobe CLI are available.
- `DIGITOR_FFMPEG_ROOT` points to an FFmpeg development SDK containing headers and link libraries.
- The runner generates deterministic CFR and VFR H.264 fixtures.
- DigitorEngine configures with `DIGITOR_REQUIRE_FFMPEG=ON`.
- FFmpeg real-media decode, VFR parity, export runtime, timeline media adapter, and unified runtime tests are registered and pass.

## Command

```powershell
Set-ExecutionPolicy -Scope Process Bypass
$env:DIGITOR_FFMPEG_ROOT = 'C:\path\to\ffmpeg-sdk'
.\tools\windows-vulkan-real-media-qualification.ps1
```

Vulkan can be run independently:

```powershell
.\tools\windows-vulkan-real-media-qualification.ps1 -SkipRealMedia
```

Real media can be run independently after the FFmpeg SDK is installed:

```powershell
.\tools\windows-vulkan-real-media-qualification.ps1 -SkipVulkan
```

Evidence is written to `artifacts/windows-vulkan-real-media-qualification`.

A complete pass ends with:

```text
VULKAN_PHYSICAL_GPU=PASS
REAL_MEDIA_PIPELINE=PASS
WINDOWS_VULKAN_REAL_MEDIA_QUALIFICATION=PASS
```
