import 'package:digitor_engine_ffi/digitor_engine_ffi.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('export config exposes production defaults', () {
    const config = DigitorExportJobConfig(
      inputPath: 'input.mp4',
      outputPath: 'output.mp4',
    );
    expect(config.width, 1920);
    expect(config.height, 1080);
    expect(config.fpsNumerator, 30);
    expect(config.preferHardware, isTrue);
    expect(config.allowSoftwareFallback, isTrue);
  });

  test('terminal state contract is stable', () {
    const completed = DigitorExportJobSnapshot(
      state: DigitorExportJobState.completed,
      requestedBackend: 1,
      executedBackend: 0,
      usedFallback: true,
      processExitCode: 0,
      durationUs: 1000000,
      completedUs: 1000000,
      progress: 1,
      generation: 2,
      diagnostic: 'completed',
    );
    const running = DigitorExportJobSnapshot(
      state: DigitorExportJobState.running,
      requestedBackend: 1,
      executedBackend: 1,
      usedFallback: false,
      processExitCode: -1,
      durationUs: 1000000,
      completedUs: 0,
      progress: 0,
      generation: 1,
      diagnostic: 'running',
    );
    expect(completed.isTerminal, isTrue);
    expect(running.isTerminal, isFalse);
  });
}
