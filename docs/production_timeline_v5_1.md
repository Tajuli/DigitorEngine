# Production Timeline Engine v5.1.0

## Status

This branch starts the production timeline milestone. It is **NOT production-qualified** and must remain unmerged until the qualification checklist below is complete.

## Implemented in this slice

- Immutable revisioned timeline snapshot contract.
- Strict validation for identifiers, timing ranges, playback rate, source coverage and transition ranges.
- Half-open clip activation semantics: `[start, end)`.
- Deterministic video compositing order independent of input vector order.
- Timeline-to-source timestamp mapping for trimmed, retimed and reverse clips.
- Transition-in and transition-out progress metadata.
- Muted/disabled audio and disabled track/clip filtering.
- Deterministic audio-window scheduling in the timeline sample-rate domain.
- Standalone warning-clean C++20 contract project.
- Linux, Windows and macOS hosted CI contract matrix.

## Deliberately not claimed complete

- Mutation/edit command layer integration with the legacy `Timeline` class.
- Insert, overwrite, ripple, roll, slip and slide command qualification.
- Nested sequence recursion and cycle rejection.
- Keyframe evaluation.
- Transition renderer execution.
- Timeline-to-Render-Graph compilation.
- Decoder request scheduling and cache invalidation integration.
- Audio resampling/mixing execution.
- Flutter C ABI exposure.
- Preview/export timeline parity.
- Real-media and device qualification.

## Merge gates

1. Contract CI passes on Linux, Windows and macOS.
2. Existing full unit and install-consumer matrix remains green.
3. Legacy Timeline edits compile into immutable production snapshots.
4. Frame-boundary tests cover CFR, VFR timestamps and end-of-stream behavior.
5. Nested sequences reject cycles and preserve deterministic identity.
6. Rapid seek and stale-work rejection passes under the existing PlaybackScheduler.
7. Preview/export timeline parity uses the same snapshot revision and source timestamp set.
8. Audio scheduling is sample-accurate across 44.1 kHz, 48 kHz and 96 kHz.
9. Long-timeline stress covers at least 10,000 clips without unbounded allocation or nondeterminism.
10. Sanitizer/thread-safety runs report no race, leak or lifetime failure.

## Next implementation slices on this PR

- Snapshot compiler from the current editing Timeline model.
- Deterministic edit-command transaction and revision publication.
- Nested timeline evaluation with recursion limits and cycle detection.
- Render/decode request plan generation.
- Audio block plan and automation hooks.
- Qualification fixtures, stress tests and preview/export integration.
