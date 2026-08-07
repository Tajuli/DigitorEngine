# Windows Physical Qualification Report

Date: 2026-08-07
Commit: cddc6c2206b260a588a14e0fa1f0512315785928

## Environment
- Platform: Windows 10 x64
- Renderer Backend: Vulkan
- Qualification: Physical local machine

## Results
- Build: PASS
- CTest: 39/39 PASS
- Native GPU qualification: PASS
- Hardware decode contract: PASS
- Hardware encode contract: PASS
- Native preview presentation: PASS
- Native surface import: PASS
- Real-media preview/export parity: PASS

## Verified runtime metrics
- CPU_INVOCATIONS=0
- FALLBACK_DISPATCHES=0
- INTERMEDIATE_READBACKS=0
- INTERMEDIATE_REUPLOADS=0
- NORMAL_PREVIEW_READBACKS=0

## Color validation
- Primary grading numerical validation: PASS
- RGB Curves: PASS
- Preview/Export parity: PASS

## Overall verdict
Windows release qualification PASSED.

This report documents physical qualification performed on a real Windows machine. Android, macOS, and iOS physical qualification remain separate release gates.