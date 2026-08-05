# Production Audio Mixing & Mastering v1

DigitorEngine owns deterministic timeline audio mixing for preview and export. The subsystem supports mono and stereo float audio, multi-track summing, per-track gain, constant-power pan, mute, fade-in, fade-out, master gain, peak limiting, peak/RMS metering, integrated loudness estimation, and stable output digests.

Preview and export call the same authoritative sample-processing path. Audio processing is intentionally CPU-based because low-latency mixing and mastering are not GPU rendering workloads; the GPU-first policy remains authoritative for visual pixels. The engine does not silently change sample rate, channel layout, or processing path.

The stable C ABI accepts planar track pointers with interleaved float samples for Flutter FFI. The app owns timeline placement, volume controls, mute/solo UI and automation editing; DigitorEngine owns sample math, limiter behavior, meters and final preview/export audio.
