# Shader reflection

The neutral model records stages, entry points, descriptor set/binding, resource kind, arrays, buffer fields, matrix layout, interface locations, specialization constants, and compute group size. The current verified implementation reads SPIR-V instructions and decorations from the validated binary; it never reflects HLSL text. Malformed binaries, absent stage entry points, and layout disagreements fail explicitly. DXIL, Metal, and GLES native reflection remain unverified and therefore cannot return compile success.
