# Production Timeline Engine v5.1.0

## Runtime integration status

This milestone now includes the production timeline contract plus the first cohesive runtime-integration layer. Merge remains gated by GitHub Actions and review; no hardware qualification is fabricated.

## Implemented

- Immutable revisioned timeline snapshot contract.
- Strict timing, source-range, transition, identifier and sample-rate validation.
- Deterministic half-open clip activation and video compositing order.
- Trim, retime and reverse timeline-to-source mapping.
- Transition progress metadata.
- Sample-domain audio scheduling.
- **Legacy Timeline snapshot compiler** converting frame-based edit state into immutable microsecond snapshots.
- Nested timeline flattening with recursion/cycle guard.
- **Monotonic revision publication** through a thread-safe immutable snapshot publisher.
- **Decode/render execution plan** containing exact source timestamps, ordered render layers and transition-derived opacity.
- **Audio mix-block plan** containing source windows and destination sample ranges.
- Linux, Windows and macOS warning-as-error contract/runtime CI.
- Focused compiler, nested sequence, revision publication, video plan and audio plan tests.

## Runtime model

`Timeline edit state -> compile immutable snapshot -> validate -> publish increasing revision -> evaluate timestamp -> build decode/render/audio plan`

Consumers acquire a shared immutable snapshot. A stale or duplicate revision is rejected. Preview and export can therefore bind one revision and use the same source timestamp plan without observing partially-mutated edit state.

## Safety contracts

- Invalid snapshots fail closed.
- Revision publication is monotonic.
- Snapshot acquisition is immutable and lifetime-safe.
- Nested timeline recursion is guarded.
- Clip and track ordering is deterministic.
- Timeline timestamps use microseconds; audio destinations use integer samples.
- No renderer, decoder or audio engine silently substitutes a different timeline revision.

## Review and merge gates

1. Contract/runtime CI passes on Linux, Windows and macOS.
2. Legacy frame-to-microsecond conversion tests pass at supported rational rates.
3. Nested sequence flattening remains deterministic and cycle-safe.
4. Revision publication rejects stale/duplicate snapshots.
5. Decode/render plans preserve exact revision and source timestamps.
6. Audio mix plans preserve exact destination sample ranges.
7. Existing engine tests remain unaffected because this change is additive.

## Follow-up scope after this PR

- PlaybackScheduler binding and seek-epoch propagation.
- Keyframe automation and transition renderer execution.
- Dedicated audio mixer/DSP engine.
- Flutter/C ABI timeline session controls.
- Real-media preview/export parity, long-timeline stress and sanitizer qualification.
