import 'package:digitor_engine_ffi/digitor_engine_ffi.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('worker completion contract remains stable', () {
    expect(
      DigitorWorkerCompletion.values,
      const <DigitorWorkerCompletion>[
        DigitorWorkerCompletion.completed,
        DigitorWorkerCompletion.cancelled,
        DigitorWorkerCompletion.failed,
      ],
    );
  });

  test('worker progress exposes exact unit counts', () {
    const progress = DigitorWorkerProgress(37, 100);
    expect(progress.completedUnits, 37);
    expect(progress.totalUnits, 100);
  });
}
