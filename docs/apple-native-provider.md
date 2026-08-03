# Apple production-native provider

This package provides the shared Metal/IOSurface/VideoToolbox provider core for macOS and iOS while keeping their Flutter registrar sources platform-specific.

Release builds require a real Flutter macOS or iOS plugin source implementing `AppleFlutterTextureBridge`. The bridge must present IOSurface/CVPixelBuffer-backed Metal frames and retain the exact `ProcessedGpuFramePtr` until Flutter releases the generation. CPU pixel mapping and byte-buffer fallback are forbidden.

The provider requires Metal, IOSurface, CVPixelBuffer, CVMetalTextureCache, VideoToolbox hardware encoding, Metal completion synchronization, color/HDR attachment propagation, zero CPU readback and zero CPU staging.

macOS and iOS are installed as distinct strict-registry providers even though they reuse the same native core. Physical Mac and iPhone qualification must prove preview, H.264/HEVC and supported ProRes export, HDR/alpha metadata, lifecycle or memory-pressure recovery, output decode-and-compare and zero-copy telemetry.
