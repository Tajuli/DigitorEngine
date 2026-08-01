# Production Audio Resampler and Time Transform v5.3.0

## Scope

This milestone adds deterministic sample-rate conversion and playback-rate transformation for decoded interleaved float PCM before the production mixer.

## Implemented

- Linear interleaved-float sample-rate conversion.
- Playback-rate range from 0.25x through 4.0x.
- Non-pitch-preserving rate conversion for lightweight and fallback-free operation.
- Pitch-preserving granular overlap-add time stretch.
- Configurable grain and overlap sizes.
- Mono through eight-channel processing without implicit channel-layout substitution.
- Strict validation for sample rates, channel counts, playback rate, grain geometry, PCM layout and finite samples.
- Shared preview and export entry points with bit-identical output.
- Deterministic output-frame calculation.

## Runtime model

`decoded interleaved float PCM -> optional pitch-preserving granular time stretch -> sample-rate conversion -> production audio mixer`

## Safety contracts

- Invalid sample rates, playback rates or channel counts fail closed.
- Malformed or non-finite PCM fails closed.
- Invalid grain/overlap geometry fails closed.
- No channel remapping is performed silently.
- Preview and export call the same implementation.
- No platform audio-device fallback is introduced.

## Qualification

The focused C++20 warnings-as-errors contract runs on Linux, Windows and macOS. Tests cover identity, sample-rate conversion, duration changes, pitch-preserving stretch output, validation and preview/export deterministic parity.

## Deliberately deferred

- High-order band-limited production resampling.
- Transient-aware WSOLA search and phase-vocoder refinement.
- Formant preservation.
- Real-media A/V synchronization qualification.
- Flutter/C ABI session controls.
