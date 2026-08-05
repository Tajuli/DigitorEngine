# Production Evidence Capture Tooling v1

This milestone turns real preview/export runs into canonical hardware-qualification evidence instead of accepting a manual `tested=true` flag.

## Capture lifecycle

1. Create a capture session with platform, backend, device/GPU/driver identity, engine version, source commit and qualification thresholds.
2. Feed monotonically ordered frame samples from the actual backend runtime.
3. Exclude configured warm-up frames.
4. Record frame time, dropped/error/device-loss counters, GPU-execution observation, silent-CPU-fallback observation and preview/export digests.
5. Finalize only after minimum frame and soak thresholds are met.
6. Serialize the result with `evidence-capture-bundle-v1.schema.json`, hash the bundle and sign it in the device-lab environment.
7. Pass the resulting record to Production Hardware Evidence Qualification and then Release Candidate Assembly.

## Failure rules

Capture fails on invalid platform/backend pairing, non-finite frame timing, non-monotonic frame order, changing preview/export digests, missing GPU execution, observed silent CPU fallback, insufficient frames or insufficient soak duration.

## Production boundary

Hosted CI qualifies the capture math, lifecycle and C ABI. It cannot create real-device evidence. Device identity, driver capture, backend telemetry, artifact hashing and signing must originate from the physical Windows, Android, macOS or iOS run.
