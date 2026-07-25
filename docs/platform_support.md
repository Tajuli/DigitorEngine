# Platform support matrix

**Meaning of values:** “Configured” means a CMake/conditional source path exists. “No” means the
capability is absent from this repository. “Unverified” means plausible source exists but this
audit has no successful target build/device evidence. Conditional files alone never count as
mobile support.

| Platform | Configured | Compiles | Tested | Runs | GPU backend verified | Decode verified | Encode verified | Preview verified |
|---|---|---|---|---|---|---|---|---|
| Windows | Yes: D3D12; optional Vulkan | Unverified | No | Unverified | No | No | No | No |
| Android | Yes: GLES; optional Vulkan | Unverified | No | Unverified | No | No | No | No |
| macOS | Yes: Metal | Unverified | No | Unverified | No | No | No | No |
| iOS | Partial: Apple conditional only; no iOS toolchain/project/package | Unverified | No | Unverified | No | No | No | No |

## Evidence by platform

### Windows

CMake selects `d3d12_backend.cpp`, links D3D12/DXGI, and optionally adds Vulkan when found
(`CMakeLists.txt:46-73`). D3D12/Vulkan native allocation code exists, but neither backend submits
commands (`src/gpu/d3d12_backend.cpp:14-28`; `src/gpu/vulkan_backend.cpp:5-12`). There is no
Windows CI/build artifact, preview surface, FFmpeg decode implementation, or encoder.

### Android

CMake selects GLES and optionally Vulkan, linking EGL/GLES (`CMakeLists.txt:46-73`). GLES assumes
a current EGL context and only allocates/maps resources (`src/gpu/gles_backend.cpp:5-10`). The
decoder only labels itself MediaCodec based on a preprocessor branch (`src/media/media.cpp:5-26`).
No Gradle/NDK packaging, emulator/device run, surface bridge, MediaCodec integration, or encode
test exists.

### macOS

CMake enables Objective-C++ and links Metal/Foundation (`CMakeLists.txt:46-73`). The Metal file
allocates resources but contains no command queue or presentation (`src/gpu/metal_backend.mm:5-13`).
VideoToolbox is only a metadata selection label (`src/media/media.cpp:5-26`). No Apple build/test,
app host, surface, decoder, or encoder evidence exists.

### iOS

`current_platform` distinguishes iOS with `TARGET_OS_IPHONE` (`src/platform/platform.cpp:3-22`),
and the generic Apple CMake branch would select Metal (`CMakeLists.txt:48-51`). That is only
conditional source coverage: no iOS toolchain, Xcode project/package, simulator/device job,
lifecycle integration, `CAMetalLayer`, VideoToolbox decode, encoder, signing, or device evidence
exists. iOS support is therefore not established.

## Host audit result (not a production target claim)

The Linux host uses `native_stub.cpp` and CPU fallback (`CMakeLists.txt:52-53`;
`src/gpu/native_stub.cpp:1-2`). The clean host build and tests pass, validating portable CPU/data
paths only. They provide no evidence for any row above.
