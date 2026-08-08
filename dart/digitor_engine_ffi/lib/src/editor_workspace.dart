import 'engine.dart';
import 'node_graph.dart';
import 'platform_host.dart';
import 'production_media.dart';
import 'production_media_pipeline.dart';
import 'session.dart';

/// High-level Digitor editor workspace owned entirely by DigitorEngine.
///
/// Applications may keep UI state around this object, but they must not own
/// decoder, renderer, graph-processing, preview-host, timeline-processing, or
/// export-processing implementations. Those responsibilities stay behind this
/// facade.
final class DigitorEditorWorkspace {
  DigitorEditorWorkspace._({
    required DigitorEngine engine,
    required DigitorNodeGraph graph,
    required DigitorProductionMediaPipeline mediaPipeline,
    required DigitorFlutterPlatformHost platformHost,
    required DigitorTimelineSession timeline,
    required DigitorRendererInfo renderer,
    required DigitorFlutterHostCapabilities? hostCapabilities,
    required int selectedNode,
  })  : _engine = engine,
        _graph = graph,
        _mediaPipeline = mediaPipeline,
        _platformHost = platformHost,
        _timeline = timeline,
        _renderer = renderer,
        _hostCapabilities = hostCapabilities,
        _selectedNode = selectedNode;

  static Future<DigitorEditorWorkspace> create({
    DigitorBackend preferredBackend = DigitorBackend.automatic,
    bool allowCpuFallback = true,
    int sampleRate = 48000,
    int channels = 2,
  }) async {
    final engine = DigitorEngine.initialize(
      preferredBackend: preferredBackend,
      allowCpuFallback: allowCpuFallback,
    );
    DigitorNodeGraph? graph;
    DigitorProductionMediaPipeline? mediaPipeline;
    DigitorFlutterPlatformHost? platformHost;
    DigitorTimelineSession? timeline;
    try {
      final renderer = engine.rendererInfo;
      graph = DigitorNodeGraph.create();
      final endpoints = graph.endpoints;
      final selected = graph.addSerialAfter(endpoints.input, name: 'Grade 01');
      graph.select(selected);
      mediaPipeline = DigitorProductionMediaPipeline(renderer: renderer);
      platformHost = DigitorFlutterPlatformHost();
      timeline = DigitorTimelineSession.create(
        sampleRate: sampleRate,
        channels: channels,
        durationUs: 0,
      );
      DigitorFlutterHostCapabilities? capabilities;
      try {
        capabilities = await platformHost.capabilities();
      } catch (_) {
        capabilities = null;
      }
      return DigitorEditorWorkspace._(
        engine: engine,
        graph: graph,
        mediaPipeline: mediaPipeline,
        platformHost: platformHost,
        timeline: timeline,
        renderer: renderer,
        hostCapabilities: capabilities,
        selectedNode: selected,
      );
    } catch (_) {
      timeline?.dispose();
      mediaPipeline?.close();
      graph?.dispose();
      if (platformHost != null) await platformHost.close();
      await engine.close();
      rethrow;
    }
  }

  final DigitorEngine _engine;
  final DigitorNodeGraph _graph;
  final DigitorProductionMediaPipeline _mediaPipeline;
  final DigitorFlutterPlatformHost _platformHost;
  final DigitorTimelineSession _timeline;
  final DigitorRendererInfo _renderer;
  final DigitorFlutterHostCapabilities? _hostCapabilities;
  int? _selectedNode;
  DigitorProductionMediaSnapshot? _media;
  int _timelineRevision = 0;
  bool _closed = false;

  DigitorRendererInfo get renderer => _renderer;
  DigitorFlutterHostCapabilities? get hostCapabilities => _hostCapabilities;
  DigitorProductionMediaSnapshot? get media => _media;
  int? get selectedNode => _selectedNode;
  String get recipeIdentity => _graph.recipeIdentity;
  int get graphRevision => _graph.graphRevision;
  int get parameterRevision => _graph.parameterRevision;

  DigitorProductionMediaSnapshot openMedia(String path) {
    _ensureOpen();
    final snapshot = _mediaPipeline.open(path);
    _media = snapshot;
    _timelineRevision += 1;
    _timeline.publish(
      revision: _timelineRevision,
      durationUs: 0,
      videoTrackCount: 1,
      audioTrackCount: 1,
    );
    return snapshot;
  }

  DigitorTimelineStatus timelineStatus() {
    _ensureOpen();
    return _timeline.status();
  }

  DigitorTimelineTelemetry timelineTelemetry() {
    _ensureOpen();
    return _timeline.telemetry();
  }

  void play() {
    _ensureOpen();
    _timeline.play();
  }

  void pause() {
    _ensureOpen();
    _timeline.pause();
  }

  void stop() {
    _ensureOpen();
    _timeline.stop();
  }

  void seek(int positionUs) {
    _ensureOpen();
    _timeline.seek(positionUs);
  }

  void setAudioControls({
    required double masterGainDb,
    required double playbackRate,
    required bool preservePitch,
    required bool enableDynamics,
  }) {
    _ensureOpen();
    _timeline.setAudioControls(
      masterGainDb: masterGainDb,
      playbackRate: playbackRate,
      preservePitch: preservePitch,
      enableDynamics: enableDynamics,
    );
  }

  void selectNode(int node) {
    _ensureOpen();
    _graph.select(node);
    _selectedNode = node;
  }

  int addSerialNode({String name = 'Serial Node'}) {
    _ensureOpen();
    final after = _selectedNode ?? _graph.endpoints.input;
    final node = _graph.addSerialAfter(after, name: name);
    _graph.select(node);
    _selectedNode = node;
    return node;
  }

  DigitorParallelNodes addParallelNodes() {
    _ensureOpen();
    final after = _selectedNode ?? _graph.endpoints.input;
    final nodes = _graph.addParallelAfter(after);
    _graph.select(nodes.first);
    _selectedNode = nodes.first;
    return nodes;
  }

  void convertSelectedToParallel() {
    _ensureSelected();
    _graph.convertToParallel(_selectedNode!);
  }

  void connectNodes(int source, int destination) {
    _ensureOpen();
    _graph.connect(source, destination);
  }

  void disconnectNodes(int source, int destination) {
    _ensureOpen();
    _graph.disconnect(source, destination);
  }

  void moveSelectedNode(double x, double y) {
    _ensureSelected();
    _graph.setPosition(_selectedNode!, x, y);
  }

  void setSelectedEnabled(bool enabled) {
    _ensureSelected();
    _graph.setEnabled(_selectedNode!, enabled);
  }

  void setSelectedBypassed(bool bypassed) {
    _ensureSelected();
    _graph.setBypassed(_selectedNode!, bypassed);
  }

  void removeSelectedNode() {
    _ensureOpen();
    final selected = _selectedNode;
    if (selected == null) return;
    _graph.remove(selected);
    final endpoints = _graph.endpoints;
    final nativeSelection = _graph.selectedNode;
    _selectedNode = nativeSelection == endpoints.input || nativeSelection == endpoints.output
        ? null
        : nativeSelection;
  }

  void clearSelectedOperations() {
    _ensureSelected();
    _graph.clearOperations(_selectedNode!);
  }

  void addCorrection(DigitorCorrection value) {
    _ensureSelected();
    _graph.addCorrection(value);
  }

  void addPrimaryWheels(DigitorPrimaryWheels value) {
    _ensureSelected();
    _graph.addPrimaryWheels(value);
  }

  void addLogWheels(DigitorLogWheels value) {
    _ensureSelected();
    _graph.addLogWheels(value);
  }

  void addRgbCurves(DigitorRgbCurves value) {
    _ensureSelected();
    _graph.addRgbCurves(value);
  }

  void addHslQualifier(DigitorHslQualifier value) {
    _ensureSelected();
    _graph.addHslQualifier(value);
  }

  void addLut1d(List<DigitorLutColor> values) {
    _ensureSelected();
    _graph.addLut1d(values);
  }

  void addLut3d(
    int edgeSize,
    List<DigitorLutColor> values, {
    DigitorLutInterpolation interpolation = DigitorLutInterpolation.tetrahedral,
  }) {
    _ensureSelected();
    _graph.addLut3d(edgeSize, values, interpolation: interpolation);
  }

  void addEffect(DigitorNodeEffect value) {
    _ensureSelected();
    _graph.addEffect(value);
  }

  void addPowerWindow(DigitorPowerWindow value) {
    _ensureSelected();
    _graph.addPowerWindow(value);
  }

  void _ensureSelected() {
    _ensureOpen();
    final selected = _selectedNode;
    if (selected == null) throw StateError('Select a node first.');
    _graph.select(selected);
  }

  void _ensureOpen() {
    if (_closed) throw StateError('DigitorEditorWorkspace is closed.');
  }

  Future<void> close() async {
    if (_closed) return;
    _closed = true;
    _timeline.dispose();
    _mediaPipeline.close();
    _graph.dispose();
    await _platformHost.close();
    await _engine.close();
  }
}
