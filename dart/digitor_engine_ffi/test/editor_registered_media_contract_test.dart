import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

void main() {
  test('production editor bypasses auxiliary FFmpeg media preflight', () {
    final controller = File(
      'lib/src/editor_controller.dart',
    ).readAsStringSync();
    final workspace = File('lib/src/editor_workspace.dart').readAsStringSync();

    expect(controller, contains('_workspace.openRegisteredMedia(path);'));
    expect(controller, isNot(contains('_workspace.openMedia(path);')));

    final methodStart = workspace.indexOf(
      'void openRegisteredMedia(String path)',
    );
    expect(methodStart, greaterThanOrEqualTo(0));
    final nextMember = workspace.indexOf(
      'DigitorPreviewCapabilities productionPreviewCapabilities()',
      methodStart,
    );
    expect(nextMember, greaterThan(methodStart));

    final registeredOpen = workspace.substring(methodStart, nextMember);
    expect(registeredOpen, isNot(contains('_mediaPipeline.open(')));
    expect(
      registeredOpen,
      contains('_productionSession = DigitorRegisteredProductionSession.open('),
    );
  });
}
