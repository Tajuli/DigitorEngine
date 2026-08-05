# Production GPU Execution Proof v1

DigitorEngine must not report a visual pass as GPU-complete merely because a backend callback returned success. This contract qualifies a native GPU execution only when the platform/backend tuple is valid, command and queue handles are present, distinct input/output resources are bound, submission and completion are observed, monotonic GPU timestamps prove device work, an output digest proves that the destination was written, no backend errors occurred, and no CPU fallback was observed.

Supported tuples are Windows with Vulkan or Direct3D 12, Android with Vulkan or OpenGL ES, and macOS/iOS with Metal. CPU is intentionally rejected by the GPU proof validator. A sequence validator rejects replayed or non-monotonic submission/completion values.

The stable C ABI `digitor_validate_gpu_execution_proof` is suitable for Flutter FFI and release-evidence capture. Platform recorders should populate the proof from native command submission and completion primitives: Vulkan command buffers/queues/timeline semaphores and timestamp queries, D3D12 command lists/queues/fences and timestamp query heaps, Metal command buffers/queues/completion handlers and GPU timestamps, or GLES command submission/fences/timer queries where supported.

This subsystem does not turn a dispatch-only effect into a real shader implementation. It prevents false GPU-success claims and provides the mandatory qualification result that real backend recorders must attach to preview and export evidence. A selected GPU backend must return failure rather than invoke a CPU reference renderer silently.
