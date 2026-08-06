# DigitorEngine release qualification

## Main-branch workflow audit

The repository contains a large number of specialized and historical workflows. They are useful for narrow subsystem qualification, but they do not form one authoritative release decision because:

- production release-candidate and resilience workflows build only dedicated contract harnesses rather than the complete engine test suite;
- native build/install qualification does not run the complete test suite;
- Flutter plugin qualification is path-filtered and does not run for every engine release candidate or version tag;
- FFmpeg, Android NDK, sanitizers, package consumers, version policy and app-integration checks are spread across independent workflows;
- no existing aggregate job requires all portable release gates to succeed together;
- compile, mock and simulator results must remain distinct from physical GPU/device qualification.

## Authoritative portable release gate

`.github/workflows/release-qualification.yml` is the consolidated portable release gate for version 5.51 and later.

It runs on:

- pull requests targeting `main`;
- pushes to `main`;
- pushes to the active app-integration RC branch;
- version tags matching `v*`;
- manual workflow dispatch.

The final `Release gate` job succeeds only when all of the following succeed:

1. source, API-boundary and canonical-version policy;
2. complete Linux, Windows and macOS build/test/install;
3. installed CMake consumer build and execution;
4. Linux AddressSanitizer and UndefinedBehaviorSanitizer test suite;
5. FFmpeg-required build plus media, decode, timeline-render and export tests;
6. Android NDK arm64-v8a and x86_64 library builds;
7. Flutter/Dart formatting, analysis and tests;
8. production release-candidate and resilience contract suites.

Installed desktop packages are retained as workflow artifacts for inspection.

## Release decision policy

A portable RC or source/package release may be produced only when the aggregate `Release gate` passes for the exact commit or tag being released.

This portable gate does not prove:

- physical Vulkan, D3D12, Metal or OpenGL ES execution;
- real Flutter native-texture registration;
- zero-copy decode/import/present/encode interoperability;
- real hardware encoder output;
- device-loss, thermal or long-duration behavior on physical devices.

Those claims require separate physical-device evidence. A release may be labeled app-integration RC or portable package release while those hardware claims remain explicitly unsupported or hardware-unverified. It must not be labeled universally hardware-qualified.

## Branch protection recommendation

After this workflow is merged to `main`, configure the `Release gate` check as a required status check for `main`. Keep direct pushes to `main` disabled and require the branch to be up to date before merge.
