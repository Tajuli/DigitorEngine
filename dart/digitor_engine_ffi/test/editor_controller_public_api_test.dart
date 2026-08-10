import 'package:digitor_engine_ffi/digitor_engine_ffi.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('editor state exposes UI-safe preview and operation state', () {
    const state = DigitorEditorState(
      mediaPath: 'clip.mp4',
      textureId: 7,
      previewGeneration: 3,
      previewTimestampUs: 1000,
      previewWidth: 1920,
      previewHeight: 1080,
      selectedNode: 5,
      graphRevision: 8,
      parameterRevision: 9,
      busy: true,
      exporting: false,
    );

    expect(state.hasMedia, isTrue);
    expect(state.hasPreview, isTrue);
    expect(state.textureId, 7);
    expect(state.selectedNode, 5);
    expect(state.graphRevision, 8);
    expect(state.parameterRevision, 9);
  });

  test('editor state copyWith can clear nullable UI state', () {
    const state = DigitorEditorState(
      mediaPath: 'clip.mp4',
      textureId: 7,
      selectedNode: 5,
      error: 'failed',
    );

    final next = state.copyWith(
      clearTextureId: true,
      clearSelectedNode: true,
      clearError: true,
    );

    expect(next.mediaPath, 'clip.mp4');
    expect(next.textureId, isNull);
    expect(next.selectedNode, isNull);
    expect(next.error, isNull);
  });
}
