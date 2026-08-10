import 'platform_host.dart';
import 'registered_production_session.dart';

/// Flutter app bootstrap status for the production editing path.
///
/// This is intentionally fail-closed: a Flutter texture plugin being attached
/// does not imply that the process-wide production decode/render/export host is
/// registered. UI integrations can inspect [ready] before opening media.
final class DigitorFlutterProductionBootstrap {
  const DigitorFlutterProductionBootstrap({
    required this.platform,
    required this.textureHostReady,
    required this.productionHostRegistered,
    required this.diagnostic,
  });

  final String platform;
  final bool textureHostReady;
  final bool productionHostRegistered;
  final String diagnostic;

  bool get ready => textureHostReady && productionHostRegistered;

  static Future<DigitorFlutterProductionBootstrap> resolve() async {
    final host = DigitorFlutterPlatformHost();
    try {
      final capabilities = await host.capabilities();
      final textureReady =
          capabilities.directDescriptorPresentation ||
          capabilities.renderTargetPresentation;
      final registered = DigitorRegisteredProductionSession.hostRegistered;
      return DigitorFlutterProductionBootstrap(
        platform: capabilities.platform,
        textureHostReady: textureReady,
        productionHostRegistered: registered,
        diagnostic: !textureReady
            ? 'Flutter native texture presentation is unavailable.'
            : registered
            ? ''
            : 'Flutter texture presentation is ready, but the concrete native '
                  'production provider has not registered the process-wide host.',
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
}
