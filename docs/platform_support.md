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

## Shader/pipeline verification (2026-07-27)
| Backend | Compiler configured | Compiles | Reflection verified | Native module | Native pipeline | Hardware-tested |
|---|---|---|---|---|---|---|
| Vulkan | optional DXC + spirv-val | compile path present | SPIR-V binary reader, not run in this environment | existing `vkCreateShaderModule` path | existing compute pipeline path | not run |
| D3D12 | optional DXC | DXIL emitted then rejected | not implemented | bytecode only | existing grade PSO only | not run |
| Metal | native source compiler exists in backend | canonical translation unavailable | not implemented | existing grade library/function only | existing grade compute state only | not run |
| OpenGL ES | runtime driver compiler | canonical translation unavailable | native query incomplete | runtime shaders only | existing linked programs only | not run |
| CPU | not applicable | no GPU compilation | validated CPU contract | not applicable | registered kernels only | host tests |

## v4.5 color graph

The authoritative CPU reference builds on all desktop targets. Vulkan, D3D12,
Metal and GLES graph shader/pipeline paths are not implemented or hardware-tested;
they are not silently substituted with CPU work. Android/iOS are unverified.

## v4.6.1 native grade qualification

The source-level audit and exact qualification truth table are maintained in
[`grade_rgba32f_execution.md`](grade_rgba32f_execution.md). This host audit did
not execute qualifying Vulkan, D3D12, Metal, GLES, Android, or iOS hardware.
Native compilation is not reported as pixel validation; FP16 is unsupported.
The internal provenance/failure seams prove that failures do not silently run
the CPU reference in non-hardware tests.
