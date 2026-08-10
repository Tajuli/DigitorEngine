import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'native_production_api.dart';
import 'platform_host.dart';
import 'registered_production_session.dart';

/// Flutter app bootstrap status for the production editing path.
///
/// The engine native asset must already be loaded before [resolve] is called.
/// The Flutter plugin contributes only an opaque registrar token; registration
/// itself is executed through DigitorEngine's stable FFI ABI from Dart.
final class DigitorFlutterProductionBootstrap {
  const DigitorFlutterProductionBootstrap({
    required this.platform,
    required this.textureHostReady,
    required this.productionHostRegistered,
    required this.diagnostic,
    this.registrarToken,
  });

  final String platform;
  final bool textureHostReady;
  final bool productionHostRegistered;
  final String diagnostic;
  final int? registrarToken;

  bool get ready => textureHostReady && productionHostRegistered;

  static int _platformCode(String platform) => switch (platform) {
    'windows' => 1,
    'android' => 2,
    'macos' => 3,
    'ios' => 4,
    _ => 0,
  };

  static String _nativeRegistrationDiagnostic() {
    final value = digitorFlutterProductionPluginLastError();
    return value == nullptr ? '' : value.toDartString();
  }

  static Future<DigitorFlutterProductionBootstrap> resolve({
    bool requestRegistration = true,
  }) async {
    final host = DigitorFlutterPlatformHost();
    try {
      final capabilities = await host.capabilities();
      final textureReady =
          capabilities.directDescriptorPresentation ||
          capabilities.renderTargetPresentation;
      int? registrarToken;
      var registrationResult = 0;

      if (textureReady &&
          requestRegistration &&
          !DigitorRegisteredProductionSession.hostRegistered) {
        registrarToken = await host.productionRegistrarToken();
        final platformCode = _platformCode(capabilities.platform);
        if (platformCode == 0) {
          registrationResult = 1;
        } else {
          final attachment =
              calloc<DigitorFlutterProductionPluginAttachmentNative>();
          final identity = 'flutter.${capabilities.platform}.production-host.v1'
              .toNativeUtf8();
          try {
            attachment.ref
              ..structSize =
                  sizeOf<DigitorFlutterProductionPluginAttachmentNative>()
              ..apiVersion = 1
              ..platform = platformCode
              ..flutterTextureRegistrar = Pointer<Void>.fromAddress(
                registrarToken,
              )
              ..implementationIdentity = identity;
            registrationResult = digitorFlutterProductionPluginAttach(
              attachment,
            );
          } finally {
            calloc.free(identity);
            calloc.free(attachment);
          }
        }
      } else if (textureReady) {
        try {
          registrarToken = await host.productionRegistrarToken();
        } catch (_) {
          registrarToken = null;
        }
      }

      final registered = DigitorRegisteredProductionSession.hostRegistered;
      final nativeDiagnostic = registered
          ? ''
          : _nativeRegistrationDiagnostic();
      final diagnostic = !textureReady
          ? 'Flutter native texture presentation is unavailable.'
          : registered
          ? ''
          : nativeDiagnostic.isNotEmpty
          ? nativeDiagnostic
          : registrationResult != 0
          ? 'Concrete native production provider registration failed '
                '(result $registrationResult). The platform factory must '
                'supply real decode, render, preview and hardware encode bindings.'
          : 'Flutter texture presentation is ready, but the concrete native '
                'production provider has not registered the process-wide host.';
      return DigitorFlutterProductionBootstrap(
        platform: capabilities.platform,
        textureHostReady: textureReady,
        productionHostRegistered: registered,
        diagnostic: diagnostic,
        registrarToken: registrarToken,
      );
    } catch (error) {
      return DigitorFlutterProductionBootstrap(
        platform: 'unknown',
        textureHostReady: false,
        productionHostRegistered:
            DigitorRegisteredProductionSession.hostRegistered,
        diagnostic: 'Flutter production bootstrap failed: $error',
      );
    } finally {
      await host.close();
    }
  }

  Future<void> detach() async {
    final token = registrarToken;
    if (token == null || token == 0) return;
    final result = digitorFlutterProductionPluginDetach(
      Pointer<Void>.fromAddress(token),
    );
    if (result != 0) {
      throw StateError('Production plugin detach failed with result $result.');
    }
  }
}
