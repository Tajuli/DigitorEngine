import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

void main() {
  test('production editor bypasses auxiliary FFmpeg media preflight', () {
    final controller = File('lib/src/editor_controller.dart').readAsStringSync();
    final workspace = File('lib/src/editor_workspace.dart').readAsStringSync();

    expect(controller, contains('_workspace.openRegisteredMedia(path);'));
    expect(controller, isNot(contains('_workspace.openMedia(path);')));
    expect(workspace, contains('void openRegisteredMedia(String path)'));
    expect(
      workspace,
      contains('_productionSession = DigitorRegisteredProductionSession.open('),
    );
  });
}
