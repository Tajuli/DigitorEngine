# CPU multithreading policy

DigitorEngine selects one backend for a session. GPU sessions remain GPU-only. CPU sessions use a persistent `CpuParallelExecutor` shared by CPU rendering stages.

## Determinism

- Node and effect order stays sequential.
- Pixel-independent work is split into fixed contiguous ranges.
- A pixel is written by exactly one task.
- Preview and export use the same kernels, grain policy and precision.
- Worker scheduling cannot change the output ordering.

## Worker policy

- Default workers: `hardware_concurrency() - 1`.
- Minimum: 1 worker.
- Maximum: 32 workers.
- The calling thread participates in execution.
- Small workloads stay on the calling thread to avoid scheduling overhead.

## Integrated stages

- CPU RGBA8 copy and transparent-frame initialization.
- CPU FP32 color grading.
- CPU compiled RGB curves.

All new CPU pixel, tile, image and effect kernels must use `CpuParallelExecutor` rather than creating per-operation threads. Neighborhood effects must include deterministic halo regions and write only their owned output tile.

## Safety

The executor owns persistent workers and joins them during backend shutdown. CPU backend initialization fails explicitly if worker creation fails. GPU sessions never instantiate or use this executor.
