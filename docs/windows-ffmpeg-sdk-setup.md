# Windows FFmpeg SDK setup

DigitorEngine uses the FFmpeg project as the upstream source and BtbN/FFmpeg-Builds as the Windows binary provider linked from FFmpeg's official download guidance.

The setup script resolves the latest BtbN GitHub release at runtime instead of hard-coding a dated download URL. It prefers the FFmpeg 8.0 release-branch GPL shared Windows x64 SDK and falls back to the master GPL shared SDK only when the release-branch package is unavailable.

The package must contain:

- `bin/ffmpeg.exe`
- `bin/ffprobe.exe`
- `include/libavcodec/avcodec.h`
- `lib/avcodec.lib`

The archive SHA-256 is verified against the release's `checksums.sha256` asset before extraction.

## Install and qualify

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\tools\setup-windows-dependencies.ps1
```

Default installation location:

```text
C:\DigitorSDK\ffmpeg
```

The script sets the user-level `DIGITOR_FFMPEG_ROOT`, adds the SDK `bin` directory to the user PATH, verifies `ffmpeg` and `ffprobe`, and runs the Windows real-media qualification unless `-SkipQualification` is supplied.

## Install only

```powershell
.\tools\setup-windows-dependencies.ps1 -SkipQualification
```

## Expected markers

```text
FFMPEG_WINDOWS_SDK_SETUP=PASS
REAL_MEDIA_PIPELINE=PASS
WINDOWS_VULKAN_REAL_MEDIA_QUALIFICATION=PASS
```
