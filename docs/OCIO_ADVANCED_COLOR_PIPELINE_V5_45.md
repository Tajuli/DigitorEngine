# v5.45.0 — OpenColorIO 2.x and Advanced Color Pipeline

This milestone adds an OpenColorIO 2.x integration layer above the existing Digitor color-science and professional color-management systems. It does not replace Primary Wheels, Log Wheels, RGB Curves, HSL Qualifier, LUT processing, node execution, HDR transforms, scopes, playback, import, audio, or export.

## Runtime behavior

- `DIGITOR_ENABLE_OCIO=ON` searches for OpenColorIO 2.x.
- `DIGITOR_REQUIRE_OCIO=ON` makes a missing OCIO dependency a configure-time error.
- When OCIO is requested at runtime but was not compiled in, the API returns `DIGITOR_RESULT_BACKEND_UNAVAILABLE`; it never silently claims an OCIO transform ran.
- The existing native color-management path remains available when OCIO is explicitly disabled in `AdvancedColorPipelineConfig`.

## Supported OCIO workflows

- Config loading from a file, in-memory text, or the current OCIO environment config
- Config validation and cache identity
- Color-space transforms
- Display/view transforms
- Look transforms
- Named transforms
- File transforms and interpolation policy
- Context variables
- Forward/inverse direction
- Processor LRU cache and invalidation
- Packed RGBA CPU processing with optional strict alpha preservation
- GPU shader extraction for D3D12 HLSL, Vulkan GLSL, OpenGL ES GLSL, and Metal Shading Language
- Inventory of color spaces, named transforms, looks, displays, views, and common roles
- Diagnostics and processor/config telemetry

## Advanced pipeline contract

`OcioColorPipeline` provides one policy surface for OCIO and the existing native pipeline. Callers may validate requests before compilation, load/reload configs, process pixels or images, compile backend shader source, and observe processor-cache behavior.

GPU shader output is a backend integration contract. The host is responsible for binding any textures/uniforms required by the returned OCIO shader to the existing D3D12, Vulkan, Metal, or GLES render graph.

## Qualification boundary

Automated tests cover request validation, bypass behavior, alpha preservation, non-finite rejection, explicit dependency unavailability, native fallback, config parsing when OCIO is installed, processor caching, CPU processing, and GPU shader extraction availability.

Studio ACES configs, production LUT packages, vendor display configs, calibrated monitors, and backend texture/uniform binding remain deployment and physical-system qualification inputs rather than hard-coded engine data.
