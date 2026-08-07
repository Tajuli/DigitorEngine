import 'package:digitor_engine_ffi/digitor_engine_ffi.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('exports the high-level Flutter engine facade', () {
    expect(DigitorEngine.open, isA<Function>());
    expect(DigitorRendererBackend.vulkan.nativeValue, 1);
    expect(DigitorRendererBackend.d3d12.nativeValue, 3);
    expect(DigitorRendererBackend.cpu.nativeValue, 100);
  });

  test('engine configuration has GPU-first friendly defaults', () {
    const configuration = DigitorEngineConfiguration();

    expect(configuration.preferredBackend, DigitorRendererBackend.auto);
    expect(configuration.enableValidation, isFalse);
    expect(configuration.allowCpuFallback, isTrue);
  });
}
