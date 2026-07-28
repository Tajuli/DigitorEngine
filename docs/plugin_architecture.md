# Plugin architecture specification (design only)

Dynamic plugin loading is **not implemented** in this milestone. Existing internal `std::function`
callbacks are implementation/test seams and are not the public plugin SDK.

## Discovery and negotiation

A UTF-8 JSON manifest contains `manifest_version`, stable plugin UUID, name/vendor, plugin version,
minimum/maximum host ABI, entry-point symbol, declared modules, permissions, and SHA-256 digests.
Unknown required fields reject the plugin; optional fields are namespaced. The host loads a reviewed
library only after manifest validation, calls one C entry point with `{size, abi_version}` request and
receives a size/versioned function table. Host and plugin select the highest mutually supported ABI;
all reserved fields must be zero. No C++ types, exceptions, allocator objects, or STL cross the ABI.

## Host table, lifecycle, and ownership

The capability table supplies logging, checked allocation/free, immutable frame views, resource
creation/destruction, task scheduling, cancellation, and capability queries. Functions absent beyond
the negotiated table size are unavailable. Lifecycle is `query -> create -> register -> start -> stop
-> destroy`; partial creation must be destroyable. Memory and handles are released by the allocator or
table that created them. Borrowed data is valid only for the call. Calls return structured status plus
host-copied UTF-8 diagnostics; exceptions and process termination are contract violations.

The manifest declares `single_threaded`, `instance_thread_safe`, or `fully_thread_safe`; callbacks may
be concurrent only as declared. Registration uses stable UUIDs for effects, shaders, decoders, encoders,
and AI processors, with media types/formats, parameter schemas and deterministic capability flags.
Serialization stores plugin UUID, module UUID, schema version and opaque canonical bytes. Plugins must
provide explicit migration functions and may reject unsupported future state without data loss.

## Security and resilience

CPU time, worker count, memory, GPU memory, output dimensions, recursion and I/O are quota-controlled.
Custom shaders accept only validated SPIR-V/MSL/HLSL profiles: bounded resources, declared bindings,
no host pointers, atomics or subgroup/device features unless granted, and platform compiler validation.
Untrusted native code is not a security boundary; high-risk decoders, encoders and AI processors should
run in a sandboxed worker process with bounded IPC. Watchdogs cancel work; repeated faults quarantine
the plugin. Failure is isolated to its instance where possible and produces a deterministic bypass/error
frame, never silent corruption.

Unload first blocks registrations and new calls, requests cancellation, drains host-owned tasks and GPU
fences, releases instances, then unloads only when callback/resource reference counts are zero. Timeout,
foreign threads, leaked resources, or a fault pins/quarantines the module until process exit rather than
risking use-after-unload. Hot reload creates a separate negotiated instance and migrates state; it never
replaces code underneath live calls.
