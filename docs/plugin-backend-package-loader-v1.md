# Plugin Backend Package Loader v1

This runtime loads installed `.digitorfx` backend shader assets into the already selected native GPU backend without adding plugin-specific engine code.

## Supported asset classes

- Windows D3D12: DXIL
- Windows/Android Vulkan: SPIR-V
- Apple Metal: metallib
- Android OpenGL ES: GLSL ES 3.0/3.1

## Contract

The loader validates package identity, plugin version, selected backend, pixel format, safe relative paths, asset size, binary kind, entry points, pass descriptors, device identity and native pipeline identity. Existing backend providers create and destroy concrete pipeline objects through callbacks.

Pipeline creation must use the selected backend and exact device. Failure is returned to the app/runtime; the loader never switches backend or executes a CPU path.

Remote native DLL, SO or dylib execution is not supported. Packages contain declarative manifests and GPU shader assets only.

## Flow

```text
installed signed package
  -> safe shader asset read
  -> backend binary validation
  -> existing provider creates native pipeline
  -> exact package/device identity validation
  -> program registry registration
  -> multi-pass zero-copy runtime dispatch
```

Normal future filters/effects can be installed and applied without editing DigitorEngine source when they remain within the generic parameter, binding, format and pass contracts.
