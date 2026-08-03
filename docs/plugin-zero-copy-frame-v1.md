# Plugin Zero-Copy Frame Contract v1

This contract closes the GPU-surface gap between imported filter/effect plugins and DigitorEngine preview/export execution.

## Runtime rules

- input and output are native GPU texture handles;
- both textures must belong to the already selected backend;
- no CPU pixel pointer, readback, re-upload or post-selection CPU fallback exists in the contract;
- dimensions, timestamp, pixel format, primaries, transfer function, range and alpha mode are preserved exactly;
- preview and export for the same timestamp must use the same plugin ID, plugin version, visual-stack digest and output color encoding;
- dispatch failure is returned to the caller and never triggers silent CPU execution.

## Supported selected backends

- Windows D3D12
- Windows Vulkan
- Android Vulkan
- Android OpenGL ES
- Apple Metal

## Per-pixel accuracy boundary

The frame contract preserves the complete color encoding identity around each plugin dispatch. Numerical shader parity remains governed by the existing effects release qualification thresholds and physical-device evidence. A plugin that changes transfer, primaries, range, alpha mode or format without an explicit render-graph conversion is rejected.

## Preview/export flow

```text
native decoded/shared GPU texture
  -> plugin zero-copy input handle
  -> selected-backend shader dispatch
  -> plugin zero-copy output handle
  -> preview compositor or hardware export path
```

The app still decides whether to send preview or export requests. DigitorEngine does not inspect commercial status.
