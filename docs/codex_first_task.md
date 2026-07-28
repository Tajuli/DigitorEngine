# Codex First Task

Use this prompt in Codex:

> Review this repository as a senior C++ rendering-engine engineer. Preserve the public C ABI. Implement milestone v0.2.0 incrementally, starting with GPU capability discovery and backend selection. Add Vulkan probing for Windows and Android, Metal probing for Apple platforms, D3D12 probing for Windows fallback, and OpenGL ES probing for Android fallback. Do not add video decoding yet. Keep CPU fallback working. Add tests for backend priority, forced backend selection, unavailable backends, and fallback behavior. Update documentation and CI. Do not claim platform support unless the code compiles in the corresponding toolchain.
