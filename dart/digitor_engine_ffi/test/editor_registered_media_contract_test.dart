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

  test(
    'workspace releases auxiliary decoder before Android or Windows production open',
    () {
      final workspace = File(
        'lib/src/editor_workspace.dart',
      ).readAsStringSync();
      final methodStart = workspace.indexOf(
        'DigitorProductionMediaSnapshot openMedia(String path)',
      );
      expect(methodStart, greaterThanOrEqualTo(0));
      final nextMember = workspace.indexOf(
        'void openRegisteredMedia(String path)',
        methodStart,
      );
      expect(nextMember, greaterThan(methodStart));

      final openMedia = workspace.substring(methodStart, nextMember);
      expect(
        openMedia,
        contains('if (Platform.isAndroid || Platform.isWindows)'),
      );
      expect(openMedia, contains('_mediaPipeline.clear();'));
      expect(
        openMedia.indexOf('_mediaPipeline.clear();'),
        lessThan(
          openMedia.indexOf(
            '_productionSession = DigitorRegisteredProductionSession.open(',
          ),
        ),
      );
    },
  );
}
