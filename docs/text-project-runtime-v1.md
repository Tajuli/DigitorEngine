# Text, Project Persistence & Progress Runtime v1

This milestone builds on the merged editor visual and spatial/compositor foundations.

## Engine-owned completion

- strict UTF-8 decoding, including Bengali text input
- deterministic glyph-bitmap layout and RGBA32F text compositing
- fill, stroke and shadow rendering hooks
- versioned editor-project serialization for clip visual state
- exact project roundtrip with escaped Unicode and delimiter-safe strings
- cancellable progress callbacks for analysis, render and export work
- stable C ABI for UTF-8 validation, project persistence and progress delivery
- warning-as-error qualification on Linux, Windows and macOS

The Digitor app remains responsible for text-entry widgets, font pickers, project-browser UI and progress presentation. The engine owns validation, persisted state and pixel output.

## Scope boundary

The runtime accepts rasterized glyph coverage supplied by a font provider. It does not claim HarfBuzz shaping, FreeType font-file loading, complex-script reordering or GPU glyph-atlas upload are complete. Those native dependencies can attach to this deterministic layout/render contract without moving authoritative pixel behavior into Flutter.
