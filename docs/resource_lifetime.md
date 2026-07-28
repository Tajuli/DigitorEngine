# Processed GPU frame lifetime

The backend constructs `ProcessedGpuFrame` with an opaque `shared_ptr<void>`
whose deleter owns the typed native texture/image and synchronization objects.
The backend must make the completion token ready only after submission makes
the resource safe for preview sampling.

Acquisition validates the owning context, backend compatibility, completion,
and native owner. Cross-context and cross-backend access are rejected. Release
uses an atomic acquisition count; releasing without a matching acquisition is
rejected. Destruction releases the native owner only after the last shared frame
reference disappears. A render context must retain a resource reference while
any frame it owns remains alive, preventing context shutdown first.

Public consumers receive the neutral frame object or an opaque C wrapper—not
the value stored by the native owner. Native handles must never be serialized or
persisted. Backend implementations must arrange context-current destruction for
GLES and device-safe destruction for Vulkan, D3D12, and Metal.
