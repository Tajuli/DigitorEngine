import 'package:digitor_engine_ffi/src/engine.dart';
import 'package:digitor_engine_ffi/src/production.dart';
import 'package:digitor_engine_ffi/src/registered_production_session.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('D3D12 export defaults match the canonical production GPU frame', () {
    final contract = digitorDefaultExportFrameContract(
      DigitorBackend.direct3D12.nativeValue,
    );

    expect(contract.workingFormat, DigitorPixelFormat.rgba32Float.nativeValue);
    expect(contract.colorMetadata, 'linear-rgba32f');
  });

  test('non-D3D12 export defaults preserve the existing contract', () {
    for (final backend in <DigitorBackend>[
      DigitorBackend.vulkan,
      DigitorBackend.metal,
      DigitorBackend.openGles,
    ]) {
      final contract = digitorDefaultExportFrameContract(backend.nativeValue);
      expect(contract.workingFormat, DigitorPixelFormat.rgba16Float.nativeValue);
      expect(contract.colorMetadata, 'linear-rgba');
    }
  });
}
