import 'package:digitor_engine_ffi/digitor_engine_ffi.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('production media pipeline is part of the public SDK', () {
    const renderer = DigitorRendererInfo(
      backend: DigitorBackend.cpu,
      backendName: 'CPU',
      deviceName: 'test',
      isGpu: false,
      supportsCompute: false,
      supportsFp16: false,
      supportsFp32: true,
    );

    final pipeline = DigitorProductionMediaPipeline(renderer: renderer);
    expect(pipeline.hasMedia, isFalse);
    expect(pipeline.snapshot, isNull);
    pipeline.close();
  });

  test('workspace callers can opt out of strict zero-copy media probing', () {
    const renderer = DigitorRendererInfo(
      backend: DigitorBackend.direct3D12,
      backendName: 'Direct3D 12',
      deviceName: 'test',
      isGpu: true,
      supportsCompute: true,
      supportsFp16: true,
      supportsFp32: true,
    );

    final pipeline = DigitorProductionMediaPipeline(
      renderer: renderer,
      requireZeroCopy: false,
    );
    expect(pipeline.hasMedia, isFalse);
    expect(pipeline.snapshot, isNull);
    pipeline.close();
  });
}
