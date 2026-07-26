# Desktop platform support

Status terms follow `production_readiness.md`; configured source is not verification.

| Environment | Build/package status | Runtime/GPU status |
|---|---|---|
| Linux GCC, Debug/Release, static/shared | Implemented and verified by CI | CPU reference only; Vulkan hardware unverified |
| Linux Clang, Debug/Release, static/shared | Implemented and verified by CI | CPU reference only; Vulkan hardware unverified |
| Windows MSVC, Debug/Release, static/shared | Implemented and verified by CI | D3D12/Vulkan implemented but unverified on qualified hardware |
| macOS Apple Clang, Debug/Release, static/shared | Implemented and verified by CI | Metal implemented but unverified on qualified hardware |
| Android/iOS | Placeholder/stub | Not included in desktop qualification |

“Verified by CI” means configuration, compilation, non-hardware tests, installation, and installed C/C++
consumer execution. It does not qualify device drivers, performance, display integration, or native GPU
pixel output. Shared Windows executables rely on CMake's target runtime directory during build-tree tests;
installed consumers resolve the installed DLL from the install `bin` directory/job environment.

FFmpeg jobs use required dependency mode and generated media. Linux supports distro development packages
and pkg-config. macOS supports Homebrew/pkg-config. All desktops support `DIGITOR_FFMPEG_ROOT`; see README.
