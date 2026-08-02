# Export M1 — Production contract and reuse audit

Parent: #159. Milestone: #160.

## Non-negotiable contract

- The selected GPU export path never silently changes to CPU.
- Preview and export consume the same frozen timeline, node, color and render revisions.
- A claimed zero-copy path performs no CPU staging and no CPU readback.
- CPU/reference export is a separate, explicit job selected before execution.
- Public ABI remains backend-neutral; native handles stay behind adapters.
- Primary Wheels v4.8.0 is reused unchanged unless a concrete defect is proven.

## Source-level reuse matrix

| Module | Status | M1 decision |
|---|---|---|
| `ProcessedGpuFrame` ownership, readiness, context retirement and metadata | Complete contract; hardware evidence varies by backend | Reuse unchanged |
| Timeline render execution/runtime/media adapter | Complete orchestration inputs; end-to-end encoder wiring pending | Reuse in M2 |
| Native surface import and platform zero-copy import | Implemented; hardware qualification incomplete | Reuse in M3–M5 |
| Production hardware decode | Implemented contract; device evidence required | Reuse |
| `ProductionHardwareEncodeSession` | Complete fail-closed session/lifecycle contract | Reuse unchanged |
| Platform hardware encoder callback factories | Missing | Implement in M3–M5 |
| Production GPU export orchestrator | Missing | Implement in M2 |
| Encoder selection and FFmpeg subprocess transcode | Complete for file transcoding, not the strict GPU-frame path | Keep separate; never route selected GPU export through it |
| Resumable export/checkpointing | Implemented | Reuse where encoder/container semantics allow |
| Audio engine and sync | Implemented contract; final mux integration pending | Reuse in M2/M6 |
| Native preview presentation seam | Implemented contract; platform hosts/hardware evidence incomplete | Reuse for parity identity |
| Primary/Log wheels, curves, qualifier, LUT, node pipeline | Implemented modules with varying qualification status | Reuse; verify in M6 |

## Known copy/readback boundaries

1. Validation readback exposed by `ProcessedGpuFrame` is qualification-only and must never be called by normal export.
2. FFmpeg subprocess transcoding reads/decodes an input file and is not the strict timeline-GPU-frame export path.
3. Software/reference export is allowed only as a separately created CPU job.
4. Platform adapter implementations must report any unavoidable copy explicitly; such a path cannot be labelled zero-copy.

## Frozen snapshot

`ExportRenderSnapshot` is the immutable identity passed to M2 orchestration. It freezes:

- snapshot identity;
- timeline, render, node graph, color pipeline and audio revisions;
- dimensions and working pixel format;
- alpha policy;
- CFR/VFR timing and duration;
- HDR/SDR and color metadata;
- output profile;
- renderer and encoder backends;
- execution policy.

Validation rejects mixed GPU/CPU jobs, mismatched profile dimensions/timing, missing revision identity, missing color metadata, and frames that differ from the frozen format/dimensions/color metadata.

## Explicit path policy

### Hardware required

GPU renderer plus hardware encoder only. Unsupported interop, device loss, encoder failure or mux failure terminates the job. No mid-session fallback.

### Explicit CPU/reference

CPU renderer plus software encoder only. It is a new job selected before execution and is never represented as zero-copy.

### Unsupported

Fails closed with a precise diagnostic.

## M1 completion boundary

M1 defines and audits the contract. It intentionally does not implement platform encoder adapters or a second renderer/export engine. M2 must wire the existing modules to this snapshot; M3–M5 provide native adapters; M6 supplies real-media and hardware qualification.
