# Platform Qualification Matrix

This matrix distinguishes source support, automated contract coverage, and physical-device production qualification. A platform is not marked production-qualified without device evidence.

| Platform | Preferred backend order | Source/contract status | Physical-device qualification | Current status |
|---|---|---|---|---|
| Windows | Vulkan → Direct3D 12 → CPU fallback | Implemented | RTX 3080 evidence recorded | **Production-qualified on recorded environment** |
| Android | Vulkan → OpenGL ES → CPU fallback | Implemented/automated coverage present | Physical Android evidence not recorded in this sign-off | **Pending physical-device qualification** |
| macOS | Metal → CPU fallback | Implemented/automated coverage present | Physical Mac evidence not recorded in this sign-off | **Pending physical-device qualification** |
| iOS | Metal → CPU fallback | Implemented/automated coverage present | Physical iPhone/iPad evidence not recorded in this sign-off | **Pending physical-device qualification** |

## Required Android evidence

Android qualification must record at minimum:

- device model, Android version, SoC/GPU and driver identity;
- selected Vulkan or OpenGL ES hardware backend;
- rejection of software renderers;
- real-media decode and repeated seek/playback;
- Primary Wheels → Log Wheels → RGB Curves → HSL preview/export parity;
- zero CPU color-operation invocation after native GPU execution begins;
- zero silent fallback, intermediate readback and reupload;
- native preview presentation;
- long-run memory/resource stability;
- supported failure-injection fail-closed and recovery behavior.

## Required Apple evidence

macOS and iOS qualification must independently record at minimum:

- device model, OS version, Apple GPU identity;
- physical Metal backend selection;
- real-media decode and repeated seek/playback;
- native-surface/VideoToolbox integration where supported;
- Primary Wheels → Log Wheels → RGB Curves → HSL preview/export parity;
- zero CPU color-operation invocation after native GPU execution begins;
- zero silent fallback, intermediate readback and reupload;
- native preview presentation;
- long-run memory/resource stability;
- supported failure-injection fail-closed and recovery behavior.

## Release rule

A platform may be called production-qualified only when its device evidence is tied to an exact commit and includes the required final markers. Passing source-contract or hosted CI tests alone is not physical-device production qualification.
