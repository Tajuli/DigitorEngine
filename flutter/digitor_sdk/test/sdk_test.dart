import 'package:flutter_test/flutter_test.dart';
import 'package:digitor_sdk/digitor_sdk.dart';
void main(){test('validates async calls',()async{final sdk=DigitorSdk('missing');await sdk.seek(10);await expectLater(sdk.seek(-1),throwsArgumentError);});}
