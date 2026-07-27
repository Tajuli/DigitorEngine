# Pipeline cache

`ShaderCache` and `PipelineCache` are separate and mutex protected. Shader identities cover source, transitive dependencies, entry point/stage, sorted macros, compiler/version, target/profile, flags, specialization values, and ABI version. Pipeline identities cover shader binaries, reflected layout, attachments, fixed-function state, vertex layout, specialization, ABI, and mandatory device/driver identity.

A pipeline cache stores owning `shared_ptr<NativePipeline>` objects created by a backend factory. Null factories/objects are errors; hashes and integers are never pipeline objects. Native objects are not serialized. Persistent shader-cache storage and a byte-limit configuration are not exposed until real disk accounting and eviction are implemented.
