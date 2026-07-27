# Native shader compiler

DigitorEngine's canonical shader language is **HLSL**. `ShaderCompileRequest` is the single internal contract. Vulkan compilation invokes a configured DXC executable with `-spirv` and Vulkan 1.2 layout flags, then requires `spirv-val`; Direct3D compilation invokes DXC for DXIL but is rejected until native `ID3D12ShaderReflection` wiring is present. Metal and OpenGL ES translation are explicitly unavailable until SPIRV-Cross and native integration are configured—there is no source parser or CPU substitution.

Tools are discovered from `DIGITOR_DXC_ROOT`/`DIGITOR_SPIRV_TOOLS_ROOT` by CMake, or supplied at runtime with `DIGITOR_DXC`/`DIGITOR_SPIRV_VAL`. Configure never downloads executables. Compiler diagnostics and version output are retained. Includes are permitted only beneath normalized roots, tracked transitively, hashed, and cycle checked.
