# GPU-first execution audit

Audit target: `main`

## Result

No active path was found that advertises a GPU renderer while intentionally executing the live preview pipeline on the CPU.

The previous high-risk design — returning a discovery-only `DeviceBackend` after detecting an adapter — is no longer used by `create_gpu_backend()`. Backend creation delegates to `create_native_backend()`, so a renderer is selected only when an executable native backend exists.

CPU fallback remains available only as an explicit engine-initialization fallback when no GPU backend can be created and `allow_cpu_fallback` is enabled. The CPU renderer reports `backend = DIGITOR_RENDERER_CPU` and `is_gpu = 0`; it does not masquerade as a GPU.

The production real-media runtime rejects CPU-resident decoded/processed frames at the playback and Flutter-presentation boundaries.

Live curves, Primary Wheels, Log Wheels and HSL qualifier wrappers reject successful results when execution provenance detects CPU reference work, fallback invocation, or normal-preview readback.

## Enforcement added by this PR

`tools/audit_gpu_first_contract.py` and `.github/workflows/gpu-first-contract.yml` make these invariants mandatory on every pull request:

- GPU selection must instantiate a native backend, never the probe-only `DeviceBackend`.
- Live GPU operations must retain silent-CPU/readback rejection guards.
- Unified playback and Flutter presentation must reject CPU frames.
- Execution provenance must retain explicit GPU/CPU/fallback counters.

## Qualification boundary

This source audit cannot prove that a driver executes shader commands on physical hardware. Production qualification still requires native builds and real-device execution on Windows (Vulkan/D3D12), Android (Vulkan/GLES), macOS and iOS (Metal), including numerical readback and failure-injection suites.
