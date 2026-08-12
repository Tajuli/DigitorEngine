import 'package:digitor_sdk/digitor_sdk.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('validates requests before loading the native library', () async {
    final sdk = DigitorSdk('missing');

    await expectLater(sdk.seek(-1), throwsArgumentError);
    await expectLater(sdk.preview(0, 0, 1080), throwsArgumentError);
    await expectLater(sdk.export(''), throwsArgumentError);
    await expectLater(
      sdk.setColor(exposure: double.nan),
      throwsArgumentError,
    );
  });
}
