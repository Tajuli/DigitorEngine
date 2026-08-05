# Windows Production Qualification Sign-off

**Qualification date:** 2026-08-05  
**Qualified commit:** `f9447e39e74325f368931270a624b3785ed4e041`  
**Host OS:** Microsoft Windows NT 10.0.19045.0  
**CPU architecture:** AMD64  
**Physical GPU:** NVIDIA GeForce RTX 3080  
**FFmpeg SDK:** 8.1.2  

## Scope

This sign-off records production qualification evidence for the Windows GPU-first rendering and real-media pipeline on the hardware and software configuration above. It is evidence for this qualified environment and commit; it is not a blanket certification for all Windows devices, drivers, codecs, or future commits.

## Qualified gates

| Gate | Result | Evidence marker |
|---|---|---|
| D3D12 physical hardware execution | PASS | `D3D12_PHYSICAL_GPU=PASS` |
| Vulkan physical hardware execution | PASS | `VULKAN_PHYSICAL_GPU=PASS` |
| GPU-first source contract | PASS | `GPU_FIRST_SOURCE_CONTRACT=PASS` |
| FFmpeg real-media pipeline | PASS | `REAL_MEDIA_PIPELINE=PASS` |
| Preview/export numerical parity | PASS | `REAL_MEDIA_PREVIEW_EXPORT_PARITY=PASS` |
| CPU color-operation invocations | PASS | `CPU_INVOCATIONS=0` |
| Silent fallback dispatches | PASS | `FALLBACK_DISPATCHES=0` |
| Intermediate GPU-to-CPU readbacks | PASS | `INTERMEDIATE_READBACKS=0` |
| Intermediate CPU-to-GPU reuploads | PASS | `INTERMEDIATE_REUPLOADS=0` |
| Normal preview readbacks | PASS | `NORMAL_PREVIEW_READBACKS=0` |
| Repeated seek/decode stress | PASS | `REPEATED_SEEK_DECODE=PASS` |
| Long-run memory growth bound | PASS | `MEMORY_GROWTH_BOUNDED=PASS` |
| Supported simulated GPU failure fail-closed | PASS | `SIMULATED_GPU_FAILURE_FAIL_CLOSED=PASS` |
| Backend recreation/recovery | PASS | `BACKEND_RECREATION_RECOVERY=PASS` |
| Windows long-run stability qualification | PASS | `WINDOWS_LONG_RUN_STABILITY_QUALIFICATION=PASS` |

## Real-media qualification

The FFmpeg qualification used deterministic H.264/AAC fixtures and verified:

- CFR decode and audio decode;
- VFR timing/parity;
- FFmpeg export runtime;
- timeline media adapter;
- unified real-media runtime;
- independent preview and export-preencode execution of the same Primary Wheels → Log Wheels → RGB Curves → HSL Qualifier chain;
- exact frame parity on the qualified run (`max_absolute_error=0`, `rms=0`, `ssim=1`, matching hashes).

## Long-run qualification

The long-run qualification completed 120 iterations with repeated decode and seek cycles, repeated physical Vulkan preview/export parity runs, a supported native `DispatchOrDraw` failure injection, fail-closed behavior without silent CPU fallback, backend recreation, and bounded working-set growth.

## Important limitations

- The failure-injection result does **not** claim that a real Windows TDR, driver reset, GPU removal, or power interruption occurred.
- Decoder input currently enters the tested color chain as decoded CPU-resident pixels before the first GPU upload. The qualified claim is zero CPU color processing and zero intermediate GPU↔CPU round-trips after GPU processing begins.
- Hardware encode/decode support depends on device, driver, codec profile, operating-system APIs, and runtime capabilities and must remain explicitly reported rather than silently substituted.
- Any change to GPU backends, shader contracts, color operations, media decode/export, renderer policy, preview presentation, or qualification tooling requires relevant requalification.

## Sign-off decision

For commit `f9447e39e74325f368931270a624b3785ed4e041` on the recorded RTX 3080 Windows environment, the D3D12, Vulkan, FFmpeg real-media, preview/export parity, zero-silent-fallback, repeated seek/decode, bounded-memory, supported failure-injection, and backend-recovery gates are **production-qualified**.
