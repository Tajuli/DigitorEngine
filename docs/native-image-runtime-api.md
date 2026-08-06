# API flow

1. The platform plugin registers complete native still-image services for its selected GPU backend.
2. The app opens `ImageEditorRuntime` through the native image runtime API.
3. A complete provider locks the session to GPU; absent provider may select CPU only before open when allowed.
4. Preview and export reuse the same node graph and parameter revisions.
5. Codec, device-loss, cancellation and memory errors are returned explicitly.
