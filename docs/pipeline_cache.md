# Pipeline cache

`ShaderCache` and `PipelineCache` are separate and mutex protected. Shader identities cover source, transitive dependencies, entry point/stage, sorted macros, compiler/version, target/profile, flags, specialization values, and ABI version. Pipeline identities cover shader binaries, reflected layout, attachments, fixed-function state, vertex layout, specialization, ABI, and mandatory device/driver identity.

A pipeline cache stores owning `shared_ptr<NativePipeline>` objects created by a backend factory. Null factories/objects are errors; hashes and integers are never pipeline objects. Native objects are not serialized. Persistent shader-cache storage and a byte-limit configuration are not exposed until real disk accounting and eviction are implemented.

## RGB Curves caches

Coefficient/LUT compilation uses the deterministic parameter serialization documented in `rgb_curves.md`; LUT size and FP32 precision are key material. Native curve pipeline and native LUT-resource caches are not implemented and are reported unsupported, not simulated with hash-only objects.
