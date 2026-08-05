# Android Physical GPU Qualification v1

This milestone prevents Android emulators and software rasterizers from being accepted as production GPU evidence. The validator accepts only Vulkan or OpenGL ES on a physical Android device with non-empty device, renderer and driver identity; completed native submission; valid GPU timestamps; zero CPU readbacks, re-uploads and fallback dispatches; and identical preview/export output digests.

`tools/android-production-qualification.sh` verifies one authorized physical device and rejects emulator, Goldfish, Ranchu, SwiftShader, llvmpipe and softpipe identities. Hosted CI compiles and tests the deterministic qualification contract. Physical execution is intentionally restricted to a self-hosted runner labelled `android` and `digitor-gpu`.

This does not claim hardware qualification merely because hosted CI passes. A release becomes Android-qualified only after an instrumented DigitorEngine app supplies real Vulkan/GLES submission, timestamp, no-fallback and preview/export parity evidence from the connected device.

The stable C ABI `digitor_qualify_android_gpu` is suitable for Flutter FFI and device-lab evidence ingestion.
