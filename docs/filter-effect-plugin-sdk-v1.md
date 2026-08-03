# DigitorEngine Filter and Effect Plugin SDK v1

## Scope

Plugin SDK v1 provides one stable registration and execution contract for built-in and third-party filters and video effects. It intentionally does not auto-load arbitrary DLL, dylib, or shared-object files.

## Plugin kinds

- Filter plugin
- Video-effect plugin

Both kinds use `PluginRegistry`, `PluginDescriptor`, `PluginInstance`, and `PluginExecutionContext`.

## Safety tiers

### Sandboxed shader

Sandboxed plugins cannot request filesystem or network access. They are expected to use engine-owned GPU resources, command recording, parameter data, and validated shader packages.

### Trusted native

Trusted-native plugins are intended for signed first-party or verified-vendor integrations. Native binary discovery and signature verification are outside SDK v1 and must be added without weakening descriptor validation.

## Validation

Registration fails when any of these conditions is true:

- invalid or duplicate plugin ID
- ABI mismatch
- no GPU backend support
- neither SDR nor HDR support
- duplicate or invalid parameter descriptor
- sandboxed plugin requests network or filesystem access
- no CPU or GPU execution path

Execution fails closed for:

- unregistered plugin
- plugin/instance identity mismatch
- unsupported backend
- unsupported SDR/HDR mode
- unknown parameter
- out-of-range or non-finite parameter
- invalid image dimensions or buffers

## Backend flags

Plugins explicitly advertise support for Vulkan, D3D12, Metal, and OpenGL ES. The engine does not silently select a different backend or CPU fallback.

## Serialization

`PluginInstance` uses a versioned `digitor-plugin-v1` representation. Plugin IDs and parameter values are validated during deserialization. Preset and project serializers can embed this representation while retaining backward compatibility.

## Built-in adapters

- `make_filter_plugin()` exposes a `FilterPreset` through the plugin registry with a keyframeable intensity parameter.
- `make_effect_plugin()` exposes an existing `EffectType` with amount, radius, and angle parameters.

This means built-in and future third-party content share the same capability and parameter model.

## Deferred work

The following intentionally remain outside SDK v1:

- native binary discovery
- code-signature and publisher verification
- out-of-process sandbox host
- marketplace licensing
- shader package compiler and validator
- hot reload

Those features should build on this ABI rather than expose DigitorEngine internal C++ classes directly.
