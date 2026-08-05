# Windows Production Qualification v1

This qualification is the release gate for claiming that DigitorEngine executes its Windows effects path on a physical D3D12 GPU rather than WARP or another software renderer.

## Mandatory execution

Run from a PowerShell terminal at the repository root:

```powershell
.\tools\windows-production-qualification.ps1
```

For a repeat hardware-only run after the full CMake suite already passed:

```powershell
.\tools\windows-production-qualification.ps1 -SkipFullSuite
```

Evidence is written to:

```text
artifacts/windows-production-qualification/
```

## PASS criteria

The qualification fails unless all applicable criteria are met:

- the central GPU-first source contract is present;
- the full Windows Release build succeeds;
- the complete registered CTest suite succeeds unless `-SkipFullSuite` is explicitly used;
- a non-software DXGI adapter creates a D3D12 hardware device;
- WARP is never accepted as production evidence;
- all built-in effects execute for SDR RGBA8 and HDR RGBA16F surfaces;
- at least 18 native passes are submitted;
- CPU readbacks are zero;
- CPU re-uploads are zero;
- fallback dispatches are zero.

The final summary must contain:

```text
GPU_FIRST_SOURCE_CONTRACT=PASS
D3D12_PHYSICAL_GPU=PASS
WINDOWS_PRODUCTION_QUALIFICATION=PASS
```

## Evidence captured

- commit and branch identity;
- Windows and processor identity;
- GPU names, driver versions and PNP identifiers;
- complete configure/build/test logs;
- D3D12 adapter class, adapter name, vendor/device IDs and memory information;
- native pass and CPU/fallback telemetry.

## GitHub Actions boundary

GitHub-hosted Windows runners are used only to compile the qualification target. Physical-GPU qualification runs only on a self-hosted runner carrying these labels:

```text
self-hosted, Windows, X64, digitor-gpu
```

This prevents WARP availability on a hosted VM from being mistaken for real GPU production qualification.

## Scope boundary

Passing v1 qualifies the current D3D12 built-in effects execution contract on the tested Windows machine and driver. It does not by itself qualify:

- every Windows GPU and driver;
- Vulkan;
- real-media decode/import/presentation/export end to end;
- long-duration playback/export stress;
- device-loss and suspend/resume recovery.

Those remain separate evidence gates.
