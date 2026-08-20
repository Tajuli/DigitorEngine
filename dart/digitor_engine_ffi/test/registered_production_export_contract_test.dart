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
    expect(contract.colorMetadata, 'linear-rgba');
  });

  test('non-D3D12 export defaults preserve the existing contract', () {
    for (final backend in <DigitorBackend>[
      DigitorBackend.vulkan,
      DigitorBackend.metal,
      DigitorBackend.openGles,
    ]) {
      final contract = digitorDefaultExportFrameContract(backend.nativeValue);
      expect(
        contract.workingFormat,
        DigitorPixelFormat.rgba16Float.nativeValue,
      );
      expect(contract.colorMetadata, 'linear-rgba');
    }
  });

  test('export color identity is independent of float working precision', () {
    final d3d = digitorDefaultExportFrameContract(
      DigitorBackend.direct3D12.nativeValue,
    );
    final gles = digitorDefaultExportFrameContract(
      DigitorBackend.openGles.nativeValue,
    );

    expect(d3d.workingFormat, DigitorPixelFormat.rgba32Float.nativeValue);
    expect(gles.workingFormat, DigitorPixelFormat.rgba16Float.nativeValue);
    expect(d3d.colorMetadata, 'linear-rgba');
    expect(gles.colorMetadata, 'linear-rgba');
  });

  test('D3D12 RGBA8 preview texture keeps RGBA32F export working format', () {
    final contract = digitorPreviewExportFrameContract(
      rendererBackend: DigitorBackend.direct3D12.nativeValue,
      presentationFormat: DigitorPixelFormat.rgba8Unorm,
    );

    expect(contract.workingFormat, DigitorPixelFormat.rgba32Float.nativeValue);
    expect(contract.colorMetadata, 'linear-rgba');
  });

  test('D3D12 BGRA8 preview texture keeps RGBA32F export working format', () {
    final contract = digitorPreviewExportFrameContract(
      rendererBackend: DigitorBackend.direct3D12.nativeValue,
      presentationFormat: DigitorPixelFormat.bgra8Unorm,
    );

    expect(contract.workingFormat, DigitorPixelFormat.rgba32Float.nativeValue);
    expect(contract.colorMetadata, 'linear-rgba');
  });

  test('float preview descriptors preserve their exact working precision', () {
    final rgba32 = digitorPreviewExportFrameContract(
      rendererBackend: DigitorBackend.direct3D12.nativeValue,
      presentationFormat: DigitorPixelFormat.rgba32Float,
    );
    final rgba16 = digitorPreviewExportFrameContract(
      rendererBackend: DigitorBackend.vulkan.nativeValue,
      presentationFormat: DigitorPixelFormat.rgba16Float,
    );

    expect(rgba32.workingFormat, DigitorPixelFormat.rgba32Float.nativeValue);
    expect(rgba16.workingFormat, DigitorPixelFormat.rgba16Float.nativeValue);
  });
}
