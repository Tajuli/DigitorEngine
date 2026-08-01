# Dart FFI and Flutter Session Adapter v5.6.0

## Scope

This milestone adds the first supported Dart integration layer for the production timeline and audio-session C ABI.

## Package

`dart/digitor_engine_ffi`

## Runtime flow

`Flutter controller -> DigitorTimelineSession -> typed Dart FFI bindings -> opaque native session -> production timeline/audio runtime`

## Implemented

- Exact Dart FFI representations for the v5.5 C structs and functions.
- Platform library loading for Windows, Android, Linux, macOS and iOS.
- Safe session creation and deterministic disposal.
- Timeline revision publication with Dart-side monotonic validation.
- Play, pause, stop and seek commands.
- Audio control updates.
- Immutable Dart status and telemetry models.
- Native result-code exceptions with operation context.
- Disposed-session protection.
- ABI layout sanity tests.
- Dedicated format, analyze and test qualification workflow.

## Integration notes

Windows expects `digitor_engine.dll`. Android and Linux expect `libdigitor_engine.so`. Apple platforms use the process image, so the engine must be linked into the application target.

The adapter is synchronous because the current session C ABI is synchronous. Decoder, render and audio-device callbacks will be introduced with the real-media session milestone and must be marshalled to the Flutter isolate.

## Deferred

- Flutter plugin platform packaging.
- Native binary bundling for each target.
- Callback delivery through native ports.
- Real-media decoder/render/audio attachment.
- A/V sync and preview/export parity qualification.
