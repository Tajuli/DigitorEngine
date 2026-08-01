# Flutter/C ABI Timeline and Audio Sessions v5.5.0

## Scope

This milestone exposes the revisioned production timeline and audio-session control surface through a stable C ABI suitable for Dart FFI.

## Implemented

- Opaque timeline/audio session handle.
- Session configuration for sample rate, channel count and duration.
- Monotonic timeline revision publication.
- Play, pause, stop and bounded seek commands.
- Seek-epoch tracking for stale-work invalidation.
- Master gain, playback rate, pitch preservation and dynamics enable controls.
- Snapshot status and command telemetry queries.
- Null-output clearing and fail-closed validation.
- Exception containment across every C ABI boundary.
- Focused pure-C-compatible ABI contract tests.

## Runtime model

`Flutter/Dart FFI -> opaque C session handle -> revision publication and playback/audio commands -> native production timeline/audio runtime`

## Safety contracts

- Revisions must increase monotonically.
- Playback cannot start before a timeline revision is published.
- Seeks outside the published duration are rejected.
- Playback rate is limited to 0.25x through 4.0x.
- Invalid pointers and configuration fail closed.
- C++ exceptions never cross the ABI.
- Output structs are cleared before validation.

## Deliberately deferred

- Dart package and generated bindings.
- Native callback delivery to Flutter isolates.
- Real decoder/render/audio-device attachment.
- Real-media A/V synchronization qualification.
