# Production hardware decode v5.38.0

This milestone connects the engine's hardware `VideoDecoder` contract to the existing native-media and render-backend zero-copy contracts through `ProductionHardwareDecodeSession`.

## Enforced runtime path

`hardware decoder -> decoder-owned native surface -> backend-compatible zero-copy import -> ProcessedGpuFrame`

The production session fails closed when any of the following occurs:

- the decoder is not hardware accelerated;
- a decoded frame has no native surface;
- CPU pixels accompany the production native frame;
- the native surface timestamp differs from the decoded-frame timestamp;
- timestamps move backwards without an explicit seek;
- the selected renderer rejects the surface;
- an importer reports success without returning a GPU frame.

No `av_hwframe_transfer_data`, `sws_scale`, CPU RGBA staging or silent CPU fallback is performed by this coordinator.

## Platform binding

The coordinator is intentionally backend-neutral and consumes the previously implemented platform importers:

- Windows D3D11VA surfaces imported by the D3D12/Vulkan zero-copy stack;
- Apple VideoToolbox `CVPixelBuffer`/IOSurface imported by Metal;
- Android MediaCodec `AHardwareBuffer`/SurfaceTexture imported by Vulkan or OpenGL ES.

The platform adapter remains responsible for synchronization, YUV-to-linear-RGBA conversion, device ownership and native-handle lifetime.

## Qualification

The included deterministic test verifies successful native import, CPU-readback rejection, importer failure propagation and seek epoch reset. Production activation still requires the existing self-hosted Windows, Android and Apple device evidence. Hosted compilation or synthetic handles are not hardware execution evidence.
