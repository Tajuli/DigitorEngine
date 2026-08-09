import 'dart:ffi';

import 'package:digitor_engine_ffi/src/bindings.dart';
import 'package:digitor_engine_ffi/src/session.dart';
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

  test('packaged native asset resolves timeline session ABI', () {
    final session = DigitorTimelineSession.create(
      sampleRate: 48000,
      channels: 2,
      durationUs: 1000000,
    );
    session.dispose();
  });
}
