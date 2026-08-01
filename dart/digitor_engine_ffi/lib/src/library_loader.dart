import 'dart:ffi';
import 'dart:io';

final class DigitorLibraryLoader {
  const DigitorLibraryLoader._();

  static DynamicLibrary open({String? overridePath}) {
    if (overridePath != null && overridePath.isNotEmpty) {
      return DynamicLibrary.open(overridePath);
    }
    if (Platform.isWindows) return DynamicLibrary.open('digitor_engine.dll');
    if (Platform.isAndroid || Platform.isLinux) {
      return DynamicLibrary.open('libdigitor_engine.so');
    }
    if (Platform.isMacOS || Platform.isIOS) return DynamicLibrary.process();
    throw UnsupportedError('DigitorEngine is unsupported on this platform.');
  }
}
