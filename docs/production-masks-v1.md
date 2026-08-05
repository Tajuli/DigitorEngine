# Production Masks & Rotoscoping v1

DigitorEngine owns deterministic mask rasterization for rectangle, ellipse and polygon masks, including feather, expansion, opacity, inversion, tracked transforms and replace/add/subtract/intersect composition.

Preview and export call the same RGBA-independent alpha-mask path and are qualified by an identical frame digest. The C ABI exposes packed definitions and control points for Flutter. Vulkan, Direct3D 12, Metal and OpenGL ES backends receive validated native dispatch packets; GPU execution never silently falls back to CPU.

The reference path is used only when CPU execution is explicitly selected or for deterministic qualification. Motion tracking analysis may feed `MaskTransform` values without changing the rasterization contract.
