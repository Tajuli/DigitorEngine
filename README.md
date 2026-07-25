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

Current version: **v0.2.0 GPU Device Layer**

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

Not implemented yet:

- GPU rendering and command submission (discovery only is implemented)
- Native GPU texture and buffer allocation
- Video decoding
- Video encoding
- Timeline
- Shader graph
- GPU shaders
- Color engine
- Node system
- LUT engine
- Export pipeline
- Flutter integration

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
