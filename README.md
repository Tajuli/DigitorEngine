# DigitorEngine

DigitorEngine is a GPU-first, CPU-fallback, cross-platform native rendering-engine foundation written in C++20.

## Current milestone

Version `0.1.0` provides:

- Stable public C API
- Engine lifecycle
- Render-context lifecycle
- Renderer backend selection
- GPU-first backend probing
- CPU fallback
- CMake build system
- Basic tests
- Example executable
- Platform-ready project structure

This milestone does **not** yet implement video decoding, shaders, timeline rendering, export, or Flutter FFI bindings beyond the C ABI foundation.

## Target platforms

- Windows
- Android
- iOS
- macOS

## Planned renderer order

- Windows: Vulkan → Direct3D 12 → CPU
- Android: Vulkan → OpenGL ES → CPU
- iOS/macOS: Metal → CPU

The current implementation uses capability stubs so the repository compiles before native GPU backends are added.

## Build

### Windows

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### Linux development host

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Example

```bash
./build/digitor_info
```

## Public API

See `include/digitor/digitor.h`.

## Roadmap

See `docs/roadmap.md`.
