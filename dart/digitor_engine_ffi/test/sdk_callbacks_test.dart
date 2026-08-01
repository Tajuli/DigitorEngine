import 'package:digitor_engine_ffi/digitor_engine_ffi.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('native callback event preserves completion data', () {
    const event = DigitorNativeEvent(
      kind: DigitorNativeEventKind.seekCompleted,
      resultCode: 0,
    );
    expect(event.kind, DigitorNativeEventKind.seekCompleted);
    expect(event.resultCode, 0);
  });

  test('native callback event preserves progress data', () {
    const event = DigitorNativeEvent(
      kind: DigitorNativeEventKind.exportProgress,
      fraction: 0.5,
      completed: 50,
      total: 100,
    );
    expect(event.fraction, 0.5);
    expect(event.completed, 50);
    expect(event.total, 100);
  });
}
