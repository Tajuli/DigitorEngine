import 'dart:ffi';

import 'package:digitor_engine/digitor_engine.dart';
import 'package:digitor_engine/src/native_api.dart';
import 'package:test/test.dart';

final class _FakeApi implements DigitorNativeApi {
  final Pointer<DigitorNativeSession> handle = Pointer.fromAddress(1);
  int revision = 0;
  int position = 0;
  int epoch = 0;
  DigitorPlaybackState state = DigitorPlaybackState.stopped;
  int destroyed = 0;
  int playCount = 0;
  int pauseCount = 0;
  int stopCount = 0;
  int seekCount = 0;

  @override DigitorNativeCall<Pointer<DigitorNativeSession>> create(DigitorSessionConfig config) => DigitorNativeCall(0, handle);
  @override int destroy(Pointer<DigitorNativeSession> session) { destroyed++; return 0; }
  @override int publish(Pointer<DigitorNativeSession> session, int value) { if (value <= revision) return 1; revision = value; return 0; }
  @override int play(Pointer<DigitorNativeSession> session) { if (revision == 0) return 1; state = DigitorPlaybackState.playing; playCount++; return 0; }
  @override int pause(Pointer<DigitorNativeSession> session) { state = DigitorPlaybackState.paused; pauseCount++; return 0; }
  @override int stop(Pointer<DigitorNativeSession> session) { state = DigitorPlaybackState.stopped; position = 0; stopCount++; return 0; }
  @override int seek(Pointer<DigitorNativeSession> session, int frame) { position = frame; epoch++; seekCount++; return 0; }
  @override int setAudioControls(Pointer<DigitorNativeSession> session, DigitorAudioControls controls) => controls.playbackRate >= .25 && controls.playbackRate <= 4 ? 0 : 1;
  @override DigitorNativeCall<DigitorSessionStatus> getStatus(Pointer<DigitorNativeSession> session) => DigitorNativeCall(0, DigitorSessionStatus(revision: revision, positionFrames: position, seekEpoch: epoch, playbackState: state));
  @override DigitorNativeCall<DigitorSessionTelemetry> getTelemetry(Pointer<DigitorNativeSession> session) => DigitorNativeCall(0, DigitorSessionTelemetry(publishedRevisions: revision, playCommands: playCount, pauseCommands: pauseCount, stopCommands: stopCount, seekCommands: seekCount));
}

void main() {
  test('publishes, controls playback and exposes telemetry', () async {
    final api = _FakeApi();
    final session = await DigitorTimelineAudioSession.create(config: const DigitorSessionConfig(durationFrames: 1000), api: api);
    final states = <DigitorSessionStatus>[];
    final subscription = session.statusStream.listen(states.add);
    await session.publishRevision(1);
    await session.play();
    await session.seek(120);
    final telemetry = await session.telemetry();
    expect(states.last.playbackState, DigitorPlaybackState.playing);
    expect(states.last.positionFrames, 120);
    expect(telemetry.playCommands, 1);
    expect(telemetry.seekCommands, 1);
    await subscription.cancel();
    await session.dispose();
    expect(api.destroyed, 1);
  });

  test('rejects stale revisions and commands after dispose', () async {
    final session = await DigitorTimelineAudioSession.create(config: const DigitorSessionConfig(durationFrames: 10), api: _FakeApi());
    await session.publishRevision(2);
    expect(() => session.publishRevision(2), throwsArgumentError);
    await session.dispose();
    expect(() => session.play(), throwsStateError);
  });

  test('maps native failures to typed exception', () async {
    final session = await DigitorTimelineAudioSession.create(config: const DigitorSessionConfig(durationFrames: 10), api: _FakeApi());
    expect(() => session.play(), throwsA(isA<DigitorEngineException>()));
    await session.dispose();
  });
}
