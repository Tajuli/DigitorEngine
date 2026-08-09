import 'dart:ffi';

import 'package:digitor_engine_ffi/src/bindings.dart';
import 'package:test/test.dart';

void main() {
  test('native structs have stable non-zero layouts', () {
    expect(
      sizeOf<DigitorTimelineSessionConfigNative>(),
      greaterThanOrEqualTo(16),
    );
    expect(
      sizeOf<DigitorTimelinePublicationNative>(),
      greaterThanOrEqualTo(24),
    );
    expect(
      sizeOf<DigitorAudioSessionControlsNative>(),
      greaterThanOrEqualTo(18),
    );
    expect(
      sizeOf<DigitorTimelineSessionStatusNative>(),
      greaterThanOrEqualTo(64),
    );
    expect(sizeOf<DigitorTimelineSessionTelemetryNative>(), equals(56));
  });

  test('opaque timeline session pointer can be represented', () {
    final pointer = Pointer<DigitorTimelineAudioSession>.fromAddress(1);
    expect(pointer.address, 1);
  });
}
