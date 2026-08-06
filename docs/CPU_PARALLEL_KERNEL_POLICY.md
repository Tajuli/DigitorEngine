# Repository-wide CPU parallel processing policy

DigitorEngine is GPU-first. When no usable GPU is selected, engine-owned CPU-heavy work must use the shared deterministic `CpuParallelExecutor`.

## Required execution forms

1. **Independent pixels/samples** use `parallel_for` or `parallel_for_rows` with disjoint output ranges.
2. **Neighbourhood kernels** use `parallel_for_tiles` and an explicit read halo. Only the tile write rectangle may be modified.
3. **Histograms, coverage, metrics and other reductions** use `deterministic_reduce`. Partial results are merged in fixed chunk order.

Node and effect order remains sequential. Parallelism happens inside each node, so scheduling cannot reorder the processing graph.

## Migrated engine-owned hotspots

- RGBA frame copy and initialization
- FP32 primary colour grading
- RGB curves
- blur and glow
- sharpen
- vignette, grain and noise
- chromatic aberration
- motion blur
- lens distortion reference processing
- production masks and ordered coverage calculation
- spatial/temporal video denoise and ordered average-delta calculation
- denoise C API input/output conversion

The same CPU reference kernels are used by preview and export.

## External-library work

FFmpeg codecs and operating-system image/video codecs own their internal worker threads. DigitorEngine must configure their documented thread controls where available, but must not wrap one codec call in competing per-frame worker pools. File I/O and short metadata/control operations are not CPU-kernel workloads.

## Determinism

- Each output element has exactly one writer.
- Tile halos are read-only.
- Reductions merge partials by chunk index, never completion order.
- Nested use falls back to the current worker to prevent oversubscription and deadlock.
- Concurrent preview/export submissions are serialized per executor while each submitted kernel remains parallel.
- Exceptions are captured on workers and rethrown to the submitting thread.

## Review gate

A new engine-owned whole-frame CPU loop must either use the shared executor or document why it is bounded control/metadata work. Creating subsystem-specific thread pools is prohibited. GPU sessions remain GPU-only and are not redirected through this scheduler.
