# Windows D3D12 Effects Qualification v1

This qualification target executes the repository-owned D3D12 provider and built-in shader package rather than a mock callback.

It validates:

- D3D12 device and direct command queue creation
- root-signature, compute-PSO and descriptor-heap creation
- all nine stable built-in effect IDs
- RGBA8 SDR and RGBA16F HDR surfaces
- single-pass and multi-pass scheduling
- command-list submission and fence completion
- zero native-runtime CPU readback, re-upload and fallback counters

The GitHub-hosted Windows job prefers a hardware adapter and falls back to WARP only for source/driver contract validation. Its artifact records `ADAPTER_CLASS=HARDWARE` or `ADAPTER_CLASS=WARP`.

A WARP pass is not physical-device qualification. Release evidence still requires this executable on the exact release commit and a real Windows GPU, plus numerical output comparison, preview/export identity, cancellation, device-loss and long-run stability evidence.
