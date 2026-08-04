library digitor_plugin_sdk;

/// App-owned policy surface for the native Digitor plugin bridge.
enum DigitorPluginSurface { preview, export }

class DigitorPluginRequest {
  const DigitorPluginRequest({required this.pluginId, required this.version, required this.surface, this.parameters = const {}});
  final String pluginId;
  final String version;
  final DigitorPluginSurface surface;
  final Map<String, Object?> parameters;
  Map<String, Object?> toJson() => {
    'plugin_id': pluginId,
    'version': version,
    'surface': surface.name,
    'parameters': parameters,
  };
}

/// Flutter uses generated dart:ffi bindings for plugin_flutter_app_sdk.h.
/// Purchase/subscription decisions stay in the app; the engine only receives
/// requests that the app has authorized.
abstract interface class DigitorPluginPolicy {
  bool allowPreview(String pluginId, String version);
  bool allowExport(String pluginId, String version);
}
