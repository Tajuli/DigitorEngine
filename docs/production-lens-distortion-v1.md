# Production Lens Distortion v1

This feature provides deterministic Brown-Conrady radial/tangential distortion and correction for RGBA32F frames, configurable optical center, scale/aspect, edge handling, optional chromatic-aberration sampling, explicit Vulkan/D3D12/Metal/GLES dispatch validation, a packed-float C ABI, and preview/export digest parity.

GPU requests return an explicit backend-unavailable or dispatch-failed status and never silently execute the CPU reference path. Platform backends consume the validated dispatch packet for native command submission.
