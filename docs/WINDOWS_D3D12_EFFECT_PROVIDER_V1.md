# Windows D3D12 Native Effects Provider v1

This package binds `NativeEffectRuntime` to the existing Windows production D3D12 device and command queue.

## Repository-owned responsibilities

- D3D12 command allocator, command list and fence lifecycle
- default-heap UAV transient textures
- RGB8/BGRA8/RGBA16F format mapping
- resource transitions and UAV barriers
- multi-pass scheduling for blur, glow and motion blur
- queue submission and bounded fence wait
- transient release only after GPU completion
- deterministic command-list abort/recovery after dispatch failure
- shared device identity and zero-copy capability validation

## Shader package boundary

The effect shader package supplies only the pipeline/root-signature/descriptor binding and dispatch callback. It does not allocate textures, submit a second queue, read pixels to the CPU or own synchronization.

The stable `shader_package_identity` must be retained with qualification evidence.

## Surface contract

Input and output handles are native `ID3D12Resource*` values represented by `NativeEffectSurface::texture_handle`. They must:

- belong to the shared Windows provider device
- enter and leave the provider in `D3D12_RESOURCE_STATE_COMMON`
- use RGBA8, BGRA8 or RGBA16F
- not be CPU-mappable
- not alias each other

## Qualification still required

CI and physical Windows hardware must prove:

- the concrete shader dispatcher covers every built-in effect ID
- descriptor heaps and root signatures are valid for all passes
- preview and export use the same stack and device
- SDR/HDR and alpha parity
- zero readback, re-upload and fallback telemetry
- device-loss, timeout, cancellation and long-run recovery

This provider completes repository-owned D3D12 resource and submission mechanics. The shader package and physical-device evidence remain required before Windows effects qualification is signed off.
