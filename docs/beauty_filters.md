# Production Beauty Filters v1

## Built-in plugin IDs

- `beauty.skin_brighten`
- `beauty.skin_smooth`
- `beauty.even_skin_tone`
- `beauty.blemish_reduction`

All four filters preserve alpha, support SDR/HDR input, expose CPU and command-recorder paths, and share the Plugin SDK v1 parameter/serialization model.

## Production matte contract

`PluginExecutionContext` accepts an optional engine-owned R32F `skin_matte` with exactly `width * height` samples. The Digitor host should generate this matte from face detection and skin segmentation, with eyes, lips, hair, teeth, facial edges, clothing, and background excluded or protected.

When no external matte is supplied, the subsystem uses a conservative soft YCbCr/chroma detector and edge-aware refinement. This fallback is deterministic and safe for preview, but a face-aware matte is required for final product qualification across diverse skin tones, lighting, makeup, and backgrounds.

## Temporal behavior

Each plugin keeps independent matte history per `stream_id`. Sequential frames blend matte confidence according to `temporal_stability`. `scene_cut=true`, a frame discontinuity, or a resolution change resets temporal reuse. This prevents stale masks and reduces frame-to-frame flicker.

## Processing behavior

### Skin Brighten

Raises skin luminance while preserving chroma and alpha. Highlight protection limits clipping and uses a smaller lift for HDR frames.

### Skin Smooth

Uses edge-aware bilateral smoothing. Local gradients, eyes, lips, hair, and face boundaries are protected by the supplied matte plus detail and edge controls.

### Even Skin Tone

Computes a matte-weighted skin luminance/chroma target and performs bounded local normalization instead of globally shifting the frame.

### Blemish Reduction

Detects small local dark deviations against an edge-aware neighborhood estimate and selectively blends only probable blemish pixels. It does not erase stable facial features or globally blur skin texture.

## Parameters

- `amount`
- `detail_protection`
- `edge_protection`
- `temporal_stability`
- `highlight_protection`

All parameters are finite, range-validated, keyframeable values in `[0, 1]`.

## Release qualification

Production sign-off requires real-media tests covering multiple skin tones, SDR/HDR, low light, mixed light, makeup, facial hair, glasses, multiple faces, rapid motion, occlusion, scene cuts, and long exports. Preview and export must use the same matte and processing sequence. Qualification must verify no alpha changes, no background spill, no temporal flicker, no eye/lip/hair softening, and no CPU readback in native GPU paths.
