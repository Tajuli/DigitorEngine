# Native Text Pipeline v1

This milestone adds the engine-owned bridge between UTF-8 editor text and backend-ready glyph draw packets.

It includes script and direction detection for Bengali, Arabic, Devanagari and Latin text, deterministic cluster/advance data, combining-mark handling, glyph raster-provider integration, atlas caching and invalidation, and indexed GPU-ready quads.

The runtime reports whether FreeType and HarfBuzz were compiled into the host build. When they are unavailable, dependency status is explicit rather than silently claiming native shaping. The deterministic provider interface remains usable for platform font providers and qualification fixtures.

The atlas and packet output are authoritative engine data. Flutter remains responsible for the text box, font picker and gestures, while preview and export consume the same shaped runs and atlas generation.

Qualification covers Bengali script detection, Arabic right-to-left ordering, malformed UTF-8 rejection, cache reuse, atlas invalidation and GPU packet construction on Linux, Windows and macOS.
