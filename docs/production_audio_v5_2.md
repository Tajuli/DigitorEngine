# Production Audio Mixer and DSP v5.2.0

## Scope

This milestone adds a deterministic float-PCM audio mixer for timeline preview and export.

## Implemented

- sample-accurate mixing in the destination sample domain.
- Overlapping clip summation.
- Clip gain in decibels.
- Constant-power stereo pan.
- Mute and solo filtering.
- Linear fade-in and fade-out envelopes.
- Master gain.
- Optional soft limiter.
- Per-channel peak and RMS meters.
- Explicit clipped-sample telemetry.
- preview/export parity through one shared mixer implementation.
- Validation for sample rate, channel count, timing, PCM layout and control values.

## Runtime model

`timeline audio mix plan -> decoded float PCM blocks -> clip controls -> sample-accurate summation -> master gain -> soft limiter -> meters -> preview/export PCM`

## Safety contracts

- Sample-rate mismatch fails closed.
- Malformed interleaved PCM fails closed.
- Invalid gain, pan, fade or channel settings fail closed.
- Solo state excludes non-solo clips deterministically.
- Preview and export call the same mixer implementation.
- The mixer does not silently resample or change channel layout.

## Qualification

The warnings-as-errors C++20 matrix runs on Linux, Windows and macOS. Tests cover overlap placement, mute/solo, gain/pan, fades, limiter telemetry, peak and RMS meters, invalid input rejection and preview/export bit-identical output. Playback and audio contracts are repeated twenty times per runner.

## Deliberately deferred

- Production resampler.
- Time-stretch and pitch preservation.
- EQ, compressor and noise gate.
- Surround bus routing.
- Platform audio-device output.
- Integrated loudness measurement.
