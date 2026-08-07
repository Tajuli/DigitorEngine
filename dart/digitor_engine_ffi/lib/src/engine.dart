import 'native_engine.dart';
import 'node_graph.dart';

/// High-level Flutter-facing facade for DigitorEngine.
///
/// Typical applications should use this class instead of opening the native
/// library or looking up C symbols directly. The native engine remains the
/// source of truth for renderer selection and node-graph execution.
final class DigitorEngine {
  DigitorEngine._({required String? libraryPath})
      : _libraryPath = libraryPath,
        _native = DigitorNativeEngine.open(libraryPath: libraryPath);

  /// Opens the DigitorEngine Flutter adapter.
  ///
  /// [libraryPath] is optional. Platform package integration normally provides
  /// the native library automatically; an explicit path is useful for desktop
  /// development, tests, or custom embedding.
  factory DigitorEngine.open({String? libraryPath}) =>
      DigitorEngine._(libraryPath: libraryPath);

  final String? _libraryPath;
  final DigitorNativeEngine _native;

  DigitorRendererInformation? _rendererInformation;

  /// Whether the process-wide native engine has been initialized by this
  /// facade.
  bool get isInitialized => _native.isInitialized;

  /// Native DigitorEngine version reported by the loaded library.
  String get version => _native.version;

  /// Renderer information captured by the most recent successful initialize.
  ///
  /// Throws when the engine has not been initialized yet.
  DigitorRendererInformation get rendererInformation {
    final information = _rendererInformation;
    if (information == null || !isInitialized) {
      throw StateError('DigitorEngine must be initialized first.');
    }
    return information;
  }

  /// Initializes DigitorEngine and selects the renderer according to the
  /// engine's GPU-first backend policy.
  DigitorRendererInformation initialize({
    DigitorEngineConfiguration configuration =
        const DigitorEngineConfiguration(),
  }) {
    final information = _native.initialize(configuration: configuration);
    _rendererInformation = information;
    return information;
  }

  /// Creates a native production node graph owned by the caller.
  ///
  /// The caller must dispose the graph before shutting the engine down.
  DigitorNativeNodeGraph createNodeGraph() {
    if (!isInitialized) {
      throw StateError(
        'DigitorEngine must be initialized before creating a node graph.',
      );
    }
    return DigitorNativeNodeGraph.create(libraryPath: _libraryPath);
  }

  /// Shuts down the process-wide native engine.
  ///
  /// Dispose any sessions, node graphs, export jobs, and presenter resources
  /// before calling this method.
  void shutdown() {
    _native.shutdown();
    _rendererInformation = null;
  }
}
