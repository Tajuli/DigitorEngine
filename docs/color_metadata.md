# Color metadata

`color::Metadata` records pixel format, bit depth, primaries, transfer, matrix,
range, chroma location, alpha, mastering display data, content-light data, and
source/working/output identities. Every enum has unknown/unspecified state.
`resolve_metadata` accepts only an explicit fallback descriptor and returns both
a `used_fallback` flag and comma-separated decision fields; unresolved fields
are errors. There is no implicit BT.709 default.
