# Release Qualification Milestone

This milestone adds the authoritative portable release gate for DigitorEngine 5.51.

## Main audit result

The main branch contains many specialized workflows, but no single workflow previously required the complete engine test suite, installed consumer, sanitizers, FFmpeg, Android NDK, Flutter FFI, production contracts and version policy to pass together.

## Added gate

`.github/workflows/release-qualification.yml` runs all portable release requirements and exposes one final `Release gate` result. The release tag or commit is portable-release eligible only when that aggregate job succeeds.

## Hardware boundary

Physical GPU execution, Flutter native texture registration, zero-copy interop and hardware encoding remain separate physical-device qualifications and are not inferred from hosted CI.
