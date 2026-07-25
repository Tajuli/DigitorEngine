# DigitorEngine

A GPU-first, CPU-fallback, cross-platform native rendering engine for professional video editing applications.

DigitorEngine is being developed as the rendering core for **Digitor**, with a long-term goal of providing identical rendering behavior across Windows, Android, iOS, and macOS.

---

# Goals

- GPU-first rendering
- CPU fallback
- Cross-platform architecture
- Shared preview and export pipeline
- Deterministic rendering
- Stable public C API
- Flutter FFI integration
- Modular design

---

# Current Status

Current version: **v2.0.0**

Implemented:

- Engine lifecycle
- Render context lifecycle
- Stable C API
- Backend abstraction
- CPU reference backend
- Native GPU device discovery and capability reporting
- Platform priority selection (Vulkan/D3D12 on Windows, Vulkan/OpenGL ES on Android, Metal on Apple)
- CMake build system
- Unit test framework
- GitHub Actions CI
- Cross-platform project layout
- Opaque texture and buffer C handles with validated CPU allocation
- Context-scoped resource lifetime enforcement

Version 2.0 adds an unbounded dependency-ordered node/shader graph with deterministic
serialization and frame caching, `.cube` 3D and 1D LUT processing (nearest, linear,
and tetrahedral interpolation), and command-encoded GPU effects. Blur, sharpen, glow,
lens distortion, seeded noise and film grain, chromatic aberration, vignette, and
motion blur share the same command path used by preview and export. LUTs provide a
CPU reference path and command-encoded path for parity testing.

Preview and export are built through `SharedRenderer`; both therefore execute the
same render graph. Frame numbers are integral and seeded effects are deterministic.
The portable command layer supplies the CPU fallback while native selection supports
Vulkan/D3D12 on Windows, Vulkan/GLES on Android, and Metal on iOS/macOS.

---

# Target Platforms

| Platform | Status |
|----------|--------|
| Windows | Device discovery (Vulkan, D3D12) |
| Android | Device discovery (Vulkan, OpenGL ES) |
| iOS | Device discovery (Metal) |
| macOS | Device discovery (Metal) |

---

# Planned Graphics Backends

| Platform | Primary | Fallback |
|----------|----------|----------|
| Windows | Vulkan | Direct3D12 → CPU |
| Android | Vulkan | OpenGL ES → CPU |
| iOS | Metal | CPU |
| macOS | Metal | CPU |

---

# Repository Structure

```text
docs/
examples/
include/
src/
tests/
third_party/
```

---

# Build

## Windows

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## macOS / Linux

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

---

# Public API

The stable public API is located at:

```
include/digitor/digitor.h
```

Future language bindings:

- Flutter (FFI)
- C#
- Rust
- Swift
- Kotlin

---

# Roadmap

See:

```
docs/roadmap.md
```

---

# License

MIT License

## Native resource layer (0.4.0)
DigitorEngine creates opaque textures, buffers, upload/staging buffers, and samplers on Vulkan (when its SDK is detected), D3D12, Metal, and Android OpenGL ES. CPU-only builds retain deterministic host allocations. Portable formats are RGBA8 UNORM, BGRA8 UNORM (where native), RGBA16 float, and RGBA32 float; unsupported translations fail explicitly. Resources are context-owned and a context with live resources cannot be destroyed. This milestone does not include shaders, passes, command submission, codecs, timeline, preview, or export.
