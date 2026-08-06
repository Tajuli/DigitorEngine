# Native image runtime platform contract

DigitorEngine owns backend selection, session locking, metadata validation, tiling policy, graph processing, preview/export parity, cancellation and progress. The platform plugin owns the operating-system objects that cannot be constructed portably:

- Windows: WIC decoder/encoder plus the active D3D12 or Vulkan upload/readback context.
- Android: ImageDecoder/native codec plus the active Vulkan or OpenGL ES context.
- Apple: ImageIO plus the active Metal device and command queue.

A complete GPU provider is selected once before image open. After selection, decode/upload, processing, resize and export failures are returned to the application and never trigger silent CPU processing. CPU is selected only when no complete GPU provider exists and fallback is permitted before the session begins.

JPEG, PNG and WebP providers must apply EXIF orientation, preserve ICC identity when available, preserve PNG/WebP alpha, require explicit JPEG flattening for alpha sources, obey decoded-byte/dimension/tile limits, and report cancellation without publishing a partial output.
