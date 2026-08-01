# Production Timeline Engine v5.1.0

## Playback integration status

The production timeline contract, runtime compiler and revision publication layer are now connected to a dedicated asynchronous playback orchestration contract. Merge remains gated by GitHub Actions and review; no hardware qualification is fabricated.

## Implemented

- Immutable revisioned timeline snapshots and strict validation.
- Legacy frame-based Timeline snapshot compiler.
- Nested sequence flattening with recursion/cycle guard.
- Monotonic thread-safe revision publication.
- Deterministic decode, render-layer and audio mix-block execution plans.
- Asynchronous **latest-request-wins** timeline playback worker.
- Revision and **seek epoch** binding with stale completion rejection.
- Bounded **revision-aware plan cache** keyed by revision, timestamp and audio window.
- Automatic cache/work invalidation when a newer timeline revision is published.
- Pause, resume, stop, explicit seek and full invalidation controls.
- Telemetry for requests, completions, stale rejection, cache hits/misses and invalidations.
- Shared builder proving **preview/export plan identity** for the same revision and timestamp.
- Linux, Windows and macOS warning-as-error qualification.
- Repeated playback contract stress execution in CI.

## Runtime model

`edit Timeline -> immutable snapshot -> monotonic publication -> playback request -> revision/epoch capture -> execution plan cache/build -> stale-work gate -> preview/export consumer`

Every queued request carries sequence, seek epoch and timeline revision. A result is deliverable only when all three still match the active session state. Seeking, publishing a new revision or invalidating the session clears queued work and cached plans.

## Safety contracts

- Invalid or missing snapshots fail closed.
- Stale and duplicate revisions are rejected at publication.
- Older requests cannot complete after a newer request, seek or revision update.
- Cached plans never cross revision boundaries.
- Preview and export use the same execution-plan builder.
- Timeline timestamps remain microseconds; audio destinations remain integer sample ranges.
- No decoder, renderer, GPU or audio fallback is introduced.

## Review and merge gates

1. Contract, runtime and playback tests pass on Linux, Windows and macOS.
2. Repeated playback stress contract passes without hangs or stale delivery.
3. Preview/export plan identity tests preserve revision, source timestamps and layer/audio counts.
4. Seek invalidation rejects prior queued work.
5. Revision synchronization clears old plans and publishes only the new revision.
6. Bounded cache produces deterministic hits and misses.
7. Thread startup, pause/resume and shutdown complete cleanly.

## Next stage after this PR

- Keyframe automation and transition renderer execution.
- Dedicated audio mixer/DSP engine.
- Flutter/C ABI timeline session controls.
- Real-media preview/export pixel parity and long-timeline sanitizer qualification.
