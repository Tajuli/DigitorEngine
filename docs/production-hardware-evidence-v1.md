# Production Hardware Evidence v1

This milestone validates evidence captured from real Windows, Android, macOS and iOS devices before a backend is called production-qualified.

Each record binds platform/backend identity, device and driver identity, engine version and source commit, evidence SHA-256, rendered/soak counts, frame-time limits, validation and device-loss counts, preview/export digests, GPU execution observation, no-silent-CPU-fallback observation and signed attestation.

A structurally valid subset returns `blocked` with a missing-platform mask. `qualified` requires one passing real-device record for every platform. Hosted CI validates the validator and C ABI only; it does not create hardware evidence.
