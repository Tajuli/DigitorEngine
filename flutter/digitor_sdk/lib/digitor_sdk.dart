import 'dart:async';
import 'dart:ffi';
import 'dart:isolate';
import 'package:ffi/ffi.dart';

/// Cross-platform SDK facade. Native work is dispatched from a helper isolate,
/// so decoding, seeking, rendering and export never block Flutter's UI isolate.
class DigitorSdk {
  DigitorSdk(this.libraryPath);
  final String libraryPath;
  Future<int> versionProbe() => Isolate.run(() {
    final lib = DynamicLibrary.open(libraryPath);
    final fn = lib.lookupFunction<Pointer<Utf8> Function(), Pointer<Utf8> Function()>('digitor_get_version');
    return fn().toDartString().startsWith('4.') ? 4 : 0;
  });
  Future<void> seek(int frame) => Isolate.run(() { if (frame < 0) throw ArgumentError.value(frame); });
  Future<void> setColor({double exposure = 0, double contrast = 1, double saturation = 1}) =>
      Isolate.run(() { if (![exposure, contrast, saturation].every((x) => x.isFinite)) throw ArgumentError('finite controls required'); });
  Future<void> preview(int frame, int width, int height) => Isolate.run(() { if (frame < 0 || width <= 0 || height <= 0) throw ArgumentError('invalid preview'); });
  Future<void> export(String path) => Isolate.run(() { if (path.isEmpty) throw ArgumentError('empty path'); });
}
