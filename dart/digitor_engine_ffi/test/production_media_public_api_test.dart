import 'package:digitor_engine_ffi/digitor_engine_ffi.dart';
import 'package:test/test.dart';

void main() {
  test('hardware decode values match native ABI', () {
    expect(DigitorHardwareDecode.automatic.nativeValue, 0);
    expect(DigitorHardwareDecode.cpu.nativeValue, 1);
    expect(DigitorHardwareDecode.dxva.nativeValue, 2);
    expect(DigitorHardwareDecode.videoToolbox.nativeValue, 3);
    expect(DigitorHardwareDecode.mediaCodec.nativeValue, 4);
  });

  test('decoded media pixel formats match native media ABI', () {
    expect(DigitorProductionMediaPixelFormat.rgba32Float.nativeValue, 0);
    expect(DigitorProductionMediaPixelFormat.rgba8.nativeValue, 1);
    expect(DigitorProductionMediaPixelFormat.bgra8.nativeValue, 2);
    expect(DigitorProductionMediaPixelFormat.nv12.nativeValue, 3);
    expect(DigitorProductionMediaPixelFormat.yuv420p.nativeValue, 4);
    expect(DigitorProductionMediaPixelFormat.p010.nativeValue, 5);
    expect(DigitorProductionMediaPixelFormat.yuv420p10.nativeValue, 6);
  });

  test('native media handle values stay stable', () {
    expect(DigitorNativeMediaHandleType.d3d11Texture2d.nativeValue, 1);
    expect(DigitorNativeMediaHandleType.d3d12Resource.nativeValue, 2);
    expect(DigitorNativeMediaHandleType.dxgiSharedHandle.nativeValue, 3);
    expect(DigitorNativeMediaHandleType.aHardwareBuffer.nativeValue, 10);
    expect(DigitorNativeMediaHandleType.cvPixelBuffer.nativeValue, 20);
    expect(DigitorNativeMediaHandleType.metalTexture.nativeValue, 22);
    expect(DigitorNativeMediaHandleType.vulkanImage.nativeValue, 30);
  });
}
