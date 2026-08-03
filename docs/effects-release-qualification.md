# Effects Release Qualification v1

An effects backend is release-qualified only when physical-device evidence passes `qualify_effects_release`. Hosted software adapters, WARP, source compilation and API contract tests are valuable gates but remain `UNQUALIFIED` hardware evidence.

## Required backends

- Windows D3D12
- Windows Vulkan
- Android Vulkan
- Android OpenGL ES
- Apple Metal on macOS or iOS

## Required evidence per adapter

- adapter and driver identity
- shader package identity
- identical serialized visual-stack digest for preview and export
- at least 300 preview frames and 300 export frames
- zero preview/export mismatches
- SDR RMSE no greater than 1/255
- HDR RMSE no greater than 0.0005
- exact alpha preservation
- zero CPU readbacks
- zero CPU re-uploads
- zero fallback dispatches after backend selection
- at least 18,000 soak frames
- at least three successful device-loss/recreation cycles

## Release rule

All shipping backend/device classes must have `PASSED` evidence. Missing physical evidence is `UNQUALIFIED`, not passed. A numerical mismatch, CPU transfer, silent fallback, alpha error, failed recovery or insufficient soak coverage is `FAILED`.

The same effect stack, parameters, deterministic seeds, input frames, color metadata and quality mode must be used for preview and export comparisons.
