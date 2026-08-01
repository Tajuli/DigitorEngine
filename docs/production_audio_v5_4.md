# Production Audio Dynamics and Loudness v5.4.0

## Scope

This milestone completes the deterministic post-mix audio-processing chain used by timeline preview and export.

## Implemented

- Multi-band parametric EQ with low-shelf, peak and high-shelf filters.
- Per-band frequency, gain and Q controls.
- Linked-channel feed-forward compressor.
- Threshold, ratio, soft knee, attack, release and makeup gain.
- Ceiling-based linked-channel limiter with release smoothing.
- Maximum gain-reduction and limiter activity telemetry.
- Momentary, short-term and integrated loudness estimates.
- Loudness-range estimate and true-peak dBFS reporting.
- Preview/export parity through one shared implementation.
- Strict validation for sample rate, channel count, PCM layout, finite samples and DSP controls.

## Runtime model

`resampled/time-transformed float PCM -> parametric EQ -> linked compressor -> linked limiter -> loudness analysis -> preview/export PCM`

## Safety contracts

- Malformed PCM and non-finite samples fail closed.
- EQ frequencies cannot cross Nyquist.
- Invalid compressor or limiter controls fail closed.
- Processing preserves the declared channel layout.
- Preview and export call the same deterministic implementation.

## Qualification

The focused C++20 warnings-as-errors contract build runs on Linux, Windows and macOS. Tests cover identity, EQ response, compression, limiter ceiling, loudness metrics, invalid input rejection and preview/export bit-identical output.

## Deliberately deferred

- Full ITU-R BS.1770 K-weighting and certified EBU R128 gating.
- Look-ahead limiter and oversampled inter-sample peak detection.
- Multiband compression and noise reduction.
- Flutter/C ABI timeline and audio-session controls.
- Real-media A/V synchronization and device-output qualification.
