# Professional Import & Media Pool v5.43.0

This milestone adds the professional ingest layer above the existing FFmpeg decoders, hardware-decode path, timeline media adapter, playback engine and export runtime. Those completed subsystems are reused and are not rewritten.

## Implemented

- persistent-ready media asset and bin model
- stable asset IDs, source revisions and file fingerprints
- duplicate detection
- complete reusable probe result model for container, codec, duration, CFR/VFR, dimensions, bit depth, chroma, color metadata, HDR metadata, rotation, timecode and all video/audio/subtitle streams
- bins, tags, search and timeline-reference counts
- thumbnail, waveform, proxy and optimized-media job contracts with progress/state/failure telemetry
- original/proxy/optimized-media paths retained per asset
- offline and changed-media detection
- fingerprint-validated single and batch relink
- media-pool persistence callback
- image-sequence pattern, padding, range and missing-frame detection
- deterministic qualification for import, derivatives, duplicate handling, metadata, search, offline recovery, relink, persistence and image sequences

## Host adapters

The engine owns media-pool policy, identity, validation, state and lifecycle. Platform/application adapters provide filesystem access, FFmpeg probing, thumbnail rendering, waveform generation, proxy/optimized-media transcoding and durable project storage. This keeps the ingest layer cross-platform while reusing the existing production decode/render/export implementations.

## Qualification boundary

Source-level behavior and host contracts are covered by deterministic tests. Real camera fixtures, removable drives, network storage, very large libraries, codec-specific metadata, proxy interoperability and long-running background-job performance remain physical-system qualification gates rather than unverified source claims.
