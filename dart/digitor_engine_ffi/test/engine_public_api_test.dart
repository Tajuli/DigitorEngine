import 'package:digitor_engine_ffi/digitor_engine_ffi.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('public backend values match the native ABI', () {
    expect(DigitorBackend.automatic.nativeValue, 0);
    expect(DigitorBackend.vulkan.nativeValue, 1);
    expect(DigitorBackend.metal.nativeValue, 2);
    expect(DigitorBackend.direct3D12.nativeValue, 3);
    expect(DigitorBackend.openGles.nativeValue, 4);
    expect(DigitorBackend.cpu.nativeValue, 100);
  });

  test('native texture backend values match the native ABI', () {
    expect(DigitorNativeTextureBackend.none.nativeValue, 0);
    expect(DigitorNativeTextureBackend.d3d11.nativeValue, 1);
    expect(DigitorNativeTextureBackend.d3d12.nativeValue, 2);
    expect(DigitorNativeTextureBackend.vulkan.nativeValue, 3);
    expect(DigitorNativeTextureBackend.metal.nativeValue, 4);
    expect(DigitorNativeTextureBackend.openGles.nativeValue, 5);
    expect(DigitorNativeTextureBackend.androidHardwareBuffer.nativeValue, 6);
    expect(DigitorNativeTextureBackend.cpuRgba8.nativeValue, 100);
  });

  test('native texture handle values match the native ABI', () {
    expect(DigitorNativeTextureHandleType.none.nativeValue, 0);
    expect(DigitorNativeTextureHandleType.dxgiSharedHandle.nativeValue, 1);
    expect(DigitorNativeTextureHandleType.d3d11Texture.nativeValue, 2);
    expect(DigitorNativeTextureHandleType.d3d12Resource.nativeValue, 3);
    expect(DigitorNativeTextureHandleType.vkImage.nativeValue, 4);
    expect(DigitorNativeTextureHandleType.metalTexture.nativeValue, 5);
    expect(DigitorNativeTextureHandleType.cvPixelBuffer.nativeValue, 6);
    expect(DigitorNativeTextureHandleType.androidHardwareBuffer.nativeValue, 7);
    expect(DigitorNativeTextureHandleType.eglImage.nativeValue, 8);
    expect(DigitorNativeTextureHandleType.glTexture.nativeValue, 9);
    expect(DigitorNativeTextureHandleType.cpuPointer.nativeValue, 100);
  });

  test('public export enum order matches the native ABI', () {
    expect(DigitorExportFormat.mp4.index, 0);
    expect(DigitorExportFormat.mov.index, 1);
    expect(DigitorExportFormat.mkv.index, 2);
    expect(DigitorExportFormat.pngSequence.index, 3);
    expect(DigitorExportFormat.tiffSequence.index, 4);
    expect(DigitorExportFormat.exrSequence.index, 5);

    expect(DigitorVideoCodec.h264.index, 0);
    expect(DigitorVideoCodec.h265.index, 1);
    expect(DigitorVideoCodec.av1.index, 2);
  });

  test('compatibility preview is the safe default', () {
    expect(DigitorPreviewMode.compatibility.nativeValue, 0);
    expect(DigitorPreviewMode.nativeGpuStrict.nativeValue, 1);
  });

  test('Flutter SDK release version is pinned', () {
    expect(DigitorEngine.version, '0.0.1');
  });
}
