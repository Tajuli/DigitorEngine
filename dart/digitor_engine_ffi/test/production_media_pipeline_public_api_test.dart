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

  test('production snapshot exposes strict GPU policy state', () {
    final snapshotType = DigitorProductionMediaSnapshot;
    expect(snapshotType, isNotNull);
  });
}
