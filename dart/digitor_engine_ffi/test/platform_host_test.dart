import 'package:digitor_engine_ffi/digitor_engine_ffi.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  const channel = MethodChannel('digitor_engine_ffi/platform_host_test');
  final messenger =
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;

  tearDown(() async {
    messenger.setMockMethodCallHandler(channel, null);
  });

  test('capabilities keep only native GPU texture handles', () async {
    messenger.setMockMethodCallHandler(channel, (call) async {
      expect(call.method, 'capabilities');
      return <String, Object>{
        'platform': 'windows',
        'supportedHandleTypes': <int>[1, 2, 100, 0],
        'directDescriptorPresentation': true,
        'renderTargetPresentation': false,
      };
    });

    final host = DigitorFlutterPlatformHost(channel: channel);
    final capabilities = await host.capabilities();

    expect(capabilities.platform, 'windows');
    expect(capabilities.supportedHandleTypes, <DigitorNativeTextureHandleType>{
      DigitorNativeTextureHandleType.dxgiSharedHandle,
      DigitorNativeTextureHandleType.d3d11Texture,
    });
    expect(capabilities.directDescriptorPresentation, isTrue);
    expect(capabilities.renderTargetPresentation, isFalse);
    await host.close();
  });

  test('create and refresh preserve the opaque native target', () async {
    var refreshSeen = false;
    messenger.setMockMethodCallHandler(channel, (call) async {
      if (call.method == 'createTexture') {
        expect(call.arguments, <String, Object>{
          'handleType': DigitorNativeTextureHandleType.vkImage.nativeValue,
          'width': 1920,
          'height': 1080,
        });
        return <String, Object>{
          'textureId': 44,
          'nativeTargetHandle': 1234,
          'targetKind': 'android-native-window',
        };
      }
      if (call.method == 'refreshTextureTarget') {
        refreshSeen = true;
        expect(call.arguments, <String, Object>{'textureId': 44});
        return <String, Object>{
          'textureId': 44,
          'nativeTargetHandle': 5678,
          'targetKind': 'android-native-window',
        };
      }
      fail('Unexpected platform call ${call.method}');
    });

    final host = DigitorFlutterPlatformHost(channel: channel);
    final target = await host.createTexture(
      handleType: DigitorNativeTextureHandleType.vkImage,
      width: 1920,
      height: 1080,
    );
    expect(target.textureId, 44);
    expect(target.nativeTargetHandle, 1234);

    final refreshed = await host.refreshTextureTarget(target);
    expect(refreshSeen, isTrue);
    expect(refreshed.textureId, 44);
    expect(refreshed.nativeTargetHandle, 5678);
    expect(
      refreshed.requestedHandleType,
      DigitorNativeTextureHandleType.vkImage,
    );
    await host.close();
  });

  test('present forwards the complete production GPU descriptor', () async {
    MethodCall? observed;
    messenger.setMockMethodCallHandler(channel, (call) async {
      observed = call;
      return null;
    });

    final host = DigitorFlutterPlatformHost(channel: channel);
    const target = DigitorFlutterTextureTarget(
      textureId: 7,
      nativeTargetHandle: 0,
      targetKind: 'windows-gpu-surface',
      requestedHandleType: DigitorNativeTextureHandleType.dxgiSharedHandle,
    );
    const frame = DigitorNativeGpuTextureFrame(
      backend: DigitorNativeTextureBackend.d3d12,
      handleType: DigitorNativeTextureHandleType.dxgiSharedHandle,
      nativeHandle: 101,
      secondaryHandle: 102,
      width: 1280,
      height: 720,
      pixelFormat: DigitorPixelFormat.bgra8Unorm,
      alphaMode: 2,
      colorPrimaries: 9,
      transferFunction: 16,
      matrixCoefficients: 9,
      colorRange: 1,
      timestampUs: 30000,
      generation: 11,
      deviceIdentity: 201,
      contextIdentity: 202,
      acquireSyncHandle: 301,
      acquireSyncValue: 302,
      releaseSyncHandle: 401,
      releaseSyncValue: 402,
      ownershipToken: 501,
      protectedContent: false,
      readiness: DigitorNativeTextureReadiness.ready,
    );

    await host.present(target, frame);

    expect(observed?.method, 'present');
    final arguments = observed?.arguments as Map<Object?, Object?>;
    expect(arguments['textureId'], 7);
    expect(arguments['backend'], DigitorNativeTextureBackend.d3d12.nativeValue);
    expect(
      arguments['handleType'],
      DigitorNativeTextureHandleType.dxgiSharedHandle.nativeValue,
    );
    expect(arguments['nativeHandle'], 101);
    expect(arguments['secondaryHandle'], 102);
    expect(arguments['generation'], 11);
    expect(arguments['deviceIdentity'], 201);
    expect(arguments['contextIdentity'], 202);
    expect(arguments['acquireSyncHandle'], 301);
    expect(arguments['releaseSyncHandle'], 401);
    expect(arguments['ownershipToken'], 501);
    expect(
      arguments['readiness'],
      DigitorNativeTextureReadiness.ready.nativeValue,
    );
    await host.close();
  });

  test('CPU and non-ready frames cannot enter the native host', () async {
    var platformCalls = 0;
    messenger.setMockMethodCallHandler(channel, (call) async {
      platformCalls++;
      return null;
    });

    final host = DigitorFlutterPlatformHost(channel: channel);
    const target = DigitorFlutterTextureTarget(
      textureId: 1,
      nativeTargetHandle: 0,
      targetKind: 'test',
      requestedHandleType: DigitorNativeTextureHandleType.dxgiSharedHandle,
    );
    const cpuFrame = DigitorNativeGpuTextureFrame(
      backend: DigitorNativeTextureBackend.cpuRgba8,
      handleType: DigitorNativeTextureHandleType.cpuPointer,
      nativeHandle: 999,
      secondaryHandle: 0,
      width: 16,
      height: 16,
      pixelFormat: DigitorPixelFormat.rgba8Unorm,
      alphaMode: 1,
      colorPrimaries: 0,
      transferFunction: 0,
      matrixCoefficients: 0,
      colorRange: 0,
      timestampUs: 0,
      generation: 1,
      deviceIdentity: 0,
      contextIdentity: 0,
      acquireSyncHandle: 0,
      acquireSyncValue: 0,
      releaseSyncHandle: 0,
      releaseSyncValue: 0,
      ownershipToken: 1,
      protectedContent: false,
      readiness: DigitorNativeTextureReadiness.ready,
    );

    expect(() => host.present(target, cpuFrame), throwsArgumentError);
    expect(
      () => host.createTexture(
        handleType: DigitorNativeTextureHandleType.cpuPointer,
        width: 16,
        height: 16,
      ),
      throwsArgumentError,
    );
    expect(platformCalls, 0);
    await host.close();
  });

  test(
    'markFrameAvailable validates generation before platform call',
    () async {
      var calls = 0;
      messenger.setMockMethodCallHandler(channel, (call) async {
        calls++;
        expect(call.method, 'markFrameAvailable');
        expect(call.arguments, <String, Object>{
          'textureId': 9,
          'generation': 4,
        });
        return null;
      });

      final host = DigitorFlutterPlatformHost(channel: channel);
      const target = DigitorFlutterTextureTarget(
        textureId: 9,
        nativeTargetHandle: 88,
        targetKind: 'android-native-window',
        requestedHandleType: DigitorNativeTextureHandleType.vkImage,
      );

      expect(
        () => host.markFrameAvailable(target, generation: 0),
        throwsArgumentError,
      );
      await host.markFrameAvailable(target, generation: 4);
      expect(calls, 1);
      await host.close();
    },
  );

  test('production registrar token is opaque and non-zero', () async {
    messenger.setMockMethodCallHandler(channel, (call) async {
      expect(call.method, 'productionRegistrarToken');
      return 0x1234;
    });

    final host = DigitorFlutterPlatformHost(channel: channel);
    expect(await host.productionRegistrarToken(), 0x1234);
    await host.close();
  });
}
