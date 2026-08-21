import 'dart:io';

import 'engine.dart';
import 'node_graph.dart';
import 'platform_host.dart';
import 'production_media_pipeline.dart';
import 'production_timeline_session.dart';
import 'registered_production_session.dart';
import 'session.dart';

final class DigitorWorkspacePreviewState {
  const DigitorWorkspacePreviewState({
    required this.generation,
    required this.timestampUs,
    required this.width,
    required this.height,
    required this.backend,
    this.textureId,
  });

  final int generation;
  final int timestampUs;
  final int width;
  final int height;
  final DigitorNativeTextureBackend backend;
  final int? textureId;
}

/// High-level Digitor editor workspace owned entirely by DigitorEngine.
final class DigitorEditorWorkspace {
  DigitorEditorWorkspace._(
    this._engine,
    this._graph,
    this._mediaPipeline,
    this._platformHost,
    this._timeline,
    this._renderer,
    this._hostCapabilities,
    this._selectedNode,
  );

  static Future<DigitorEditorWorkspace> create({
    DigitorBackend preferredBackend = DigitorBackend.automatic,
    bool allowCpuFallback = true,
    int sampleRate = 48000,
    int channels = 2,
  }) async {
    final platformHost = DigitorFlutterPlatformHost();
    DigitorFlutterHostCapabilities? capabilities;
    try {
      capabilities = await platformHost.capabilities();
    } catch (_) {
      capabilities = null;
    }

    final resolvedBackend =
        preferredBackend == DigitorBackend.automatic &&
            Platform.isAndroid &&
            capabilities?.platform == 'android' &&
            capabilities?.renderTargetPresentation == true
        ? DigitorBackend.openGles
        : preferredBackend;

    DigitorEngine? engine;
    DigitorNodeGraph? graph;
    DigitorProductionMediaPipeline? mediaPipeline;
    DigitorTimelineSession? timeline;
    try {
      engine = DigitorEngine.initialize(
        preferredBackend: resolvedBackend,
        allowCpuFallback: allowCpuFallback,
      );
      final renderer = engine.rendererInfo;
      graph = DigitorNodeGraph.create();
      final endpoints = graph.endpoints;
      final selected = graph.addSerialAfter(endpoints.input, name: 'Grade 01');
      graph.select(selected);
      mediaPipeline = DigitorProductionMediaPipeline(renderer: renderer);
      timeline = DigitorTimelineSession.create(
        sampleRate: sampleRate,
        channels: channels,
        durationUs: 0,
      );
      return DigitorEditorWorkspace._(
        engine,
        graph,
        mediaPipeline,
        platformHost,
        timeline,
        renderer,
        capabilities,
        selected,
      );
    } catch (_) {
      timeline?.dispose();
      mediaPipeline?.close();
      graph?.dispose();
      await platformHost.close();
      if (engine != null) await engine.close();
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
  DigitorRegisteredProductionSession? _productionSession;
  DigitorProductionTimelineSession? _productionTimeline;
  DigitorFlutterTextureTarget? _previewTexture;
  int _previewWidth = 0;
  int _previewHeight = 0;
  int _timelineRevision = 0;
  int _productionTimelineRevision = 0;
  int _projectFpsNum = 30;
  int _projectFpsDen = 1;
  int _audioRevision = 1;
  int _exportSnapshotIdentity = 0;
  bool _closed = false;

  DigitorRendererInfo get renderer => _renderer;
  DigitorFlutterHostCapabilities? get hostCapabilities => _hostCapabilities;
  DigitorProductionMediaSnapshot? get media => _media;
  int? get selectedNode => _selectedNode;
  int? get previewTextureId => _previewTexture?.textureId;
  String get recipeIdentity => _graph.recipeIdentity;
  int get graphRevision => _graph.graphRevision;
  int get parameterRevision => _graph.parameterRevision;
  bool get productionHostRegistered =>
      DigitorRegisteredProductionSession.hostRegistered;
  bool get productionTimelineConfigured =>
      _productionTimeline?.configured ?? false;
  bool get productionReady =>
      _productionSession != null || productionTimelineConfigured;

  DigitorProductionMediaSnapshot openMedia(String path) {
    _ensureOpen();
    clearProductionTimeline();
    _productionSession?.dispose();
    _productionSession = null;
    final snapshot = _mediaPipeline.open(path);
    _media = snapshot;
    if (DigitorRegisteredProductionSession.hostRegistered) {
      if (Platform.isAndroid || Platform.isWindows) {
        _media = DigitorProductionMediaSnapshot(
          path: snapshot.path,
          decoder: snapshot.decoder,
          firstFrame: snapshot.firstFrame,
          duration: snapshot.duration,
          nativeSurface: null,
          strictGpuPath: snapshot.strictGpuPath,
        );
        _mediaPipeline.clear();
      }
      _productionSession = DigitorRegisteredProductionSession.open(
        mediaPath: path,
        nodeGraph: _graph,
      );
      _productionTimeline ??= DigitorProductionTimelineSession(_graph);
    }
    final media = _media!;
    _timeline.attachMedia(path);
    _timelineRevision += 1;
    _timeline.publish(
      revision: _timelineRevision,
      durationUs: media.duration.inMicroseconds,
      videoTrackCount: 1,
      audioTrackCount: 1,
    );
    return media;
  }

  void openRegisteredMedia(String path) {
    _ensureOpen();
    if (path.isEmpty) {
      throw ArgumentError.value(path, 'path', 'must not be empty');
    }
    if (!DigitorRegisteredProductionSession.hostRegistered) {
      throw StateError(
        'The native Flutter production host is not registered for this platform.',
      );
    }

    clearProductionTimeline();
    _productionSession?.dispose();
    _productionSession = null;
    _mediaPipeline.clear();
    _media = null;
    _productionSession = DigitorRegisteredProductionSession.open(
      mediaPath: path,
      nodeGraph: _graph,
    );
    _productionTimeline ??= DigitorProductionTimelineSession(_graph);
    _timeline.attachMedia(path);
    _timelineRevision += 1;
    _timeline.publish(
      revision: _timelineRevision,
      durationUs: 0,
      videoTrackCount: 1,
      audioTrackCount: 1,
    );
  }

  /// Publishes the authoritative native edit model to the native production
  /// renderer. After this succeeds, preview/export use project time and the
  /// legacy single-source decoder session is released so there is no duplicate
  /// hardware decoder competing with the project source registry.
  void configureProductionTimeline({
    required String serializedProject,
    required List<DigitorTimelineMediaSource> sources,
    required int revision,
    required int durationUs,
    required int videoTrackCount,
    required int audioTrackCount,
    int fpsNum = 30,
    int fpsDen = 1,
  }) {
    _ensureOpen();
    if (!DigitorRegisteredProductionSession.hostRegistered) {
      throw StateError('Native production host is not registered.');
    }
    if (serializedProject.isEmpty || sources.isEmpty || revision <= 0 ||
        durationUs <= 0 || videoTrackCount <= 0 || audioTrackCount < 0 ||
        fpsNum <= 0 || fpsDen <= 0) {
      throw ArgumentError('Invalid native production timeline publication.');
    }
    final timeline = _productionTimeline ??=
        DigitorProductionTimelineSession(_graph);
    timeline.configure(
      serializedProject: serializedProject,
      sources: sources,
    );
    _productionTimelineRevision = revision;
    _projectFpsNum = fpsNum;
    _projectFpsDen = fpsDen;

    // The project service owns per-source hardware decoders now. Keep only the
    // process-wide registered host, not a second open single-source decoder.
    _productionSession?.dispose();
    _productionSession = null;

    _timeline.detachMedia();
    _timelineRevision += 1;
    _timeline.publish(
      revision: _timelineRevision,
      durationUs: durationUs,
      videoTrackCount: videoTrackCount,
      audioTrackCount: audioTrackCount,
    );
  }

  void clearProductionTimeline() {
    if (_closed) return;
    final timeline = _productionTimeline;
    if (timeline != null && timeline.configured) {
      timeline.clear();
    }
    _productionTimelineRevision = 0;
    _projectFpsNum = 30;
    _projectFpsDen = 1;
  }

  DigitorPreviewCapabilities productionPreviewCapabilities() {
    _ensureProductionReady();
    if (productionTimelineConfigured) {
      return _productionTimeline!.previewCapabilities;
    }
    return _productionSession!.previewCapabilities;
  }

  DigitorWorkspacePreviewState renderPreview({
    required int timestampUs,
    required int width,
    required int height,
  }) {
    _ensureProductionReady();
    final frame = productionTimelineConfigured
        ? _productionTimeline!.preview(
            timestampUs: timestampUs,
            width: width,
            height: height,
          )
        : _productionSession!.preview(
            timestampUs: timestampUs,
            width: width,
            height: height,
          );
    return DigitorWorkspacePreviewState(
      generation: frame.generation,
      timestampUs: frame.timestampUs,
      width: frame.width,
      height: frame.height,
      backend: frame.backend,
    );
  }

  Future<DigitorWorkspacePreviewState> presentPreview({
    required int timestampUs,
    required int width,
    required int height,
  }) async {
    _ensureProductionReady();
    final capabilities = _hostCapabilities;
    if (capabilities == null) {
      throw StateError('Flutter platform texture host is unavailable.');
    }
    final previewCapabilities = productionPreviewCapabilities();
    if (capabilities.renderTargetPresentation) {
      final handleType = previewCapabilities.handleType;
      if (!capabilities.supports(handleType)) {
        throw StateError(
          'Flutter host does not support render-target handle ${handleType.name}.',
        );
      }
      var target = _previewTexture;
      final needsTexture =
          target == null ||
          target.requestedHandleType != handleType ||
          _previewWidth != width ||
          _previewHeight != height;
      if (needsTexture) {
        if (target != null) await _platformHost.disposeTexture(target);
        target = await _platformHost.createTexture(
          handleType: handleType,
          width: width,
          height: height,
        );
        _previewTexture = target;
        _previewWidth = width;
        _previewHeight = height;
      } else {
        target = await _platformHost.refreshTextureTarget(target);
        _previewTexture = target;
      }
      if (target.nativeTargetHandle == 0) {
        throw StateError('Flutter render target is not currently available.');
      }
      if (productionTimelineConfigured) {
        _productionTimeline!.setPreviewTarget(
          nativeTargetHandle: target.nativeTargetHandle,
          width: width,
          height: height,
          handleType: handleType,
        );
      } else {
        _productionSession!.setPreviewTarget(
          nativeTargetHandle: target.nativeTargetHandle,
          width: width,
          height: height,
          handleType: handleType,
        );
      }
      final frame = productionTimelineConfigured
          ? _productionTimeline!.preview(
              timestampUs: timestampUs,
              width: width,
              height: height,
            )
          : _productionSession!.preview(
              timestampUs: timestampUs,
              width: width,
              height: height,
            );
      try {
        await _platformHost.markFrameAvailable(
          target,
          generation: frame.generation,
        );
        return DigitorWorkspacePreviewState(
          generation: frame.generation,
          timestampUs: frame.timestampUs,
          width: frame.width,
          height: frame.height,
          backend: frame.backend,
          textureId: target.textureId,
        );
      } finally {
        if (productionTimelineConfigured) {
          _productionTimeline!.previewConsumed(frame.generation);
        } else {
          _productionSession!.previewConsumed(frame.generation);
        }
      }
    }

    if (!capabilities.directDescriptorPresentation) {
      throw UnsupportedError('Flutter platform exposes no production preview path.');
    }
    final frame = productionTimelineConfigured
        ? _productionTimeline!.preview(
            timestampUs: timestampUs,
            width: width,
            height: height,
          )
        : _productionSession!.preview(
            timestampUs: timestampUs,
            width: width,
            height: height,
          );
    try {
      if (!capabilities.supports(frame.handleType)) {
        throw StateError(
          'Flutter host does not support preview handle ${frame.handleType.name}.',
        );
      }
      var target = _previewTexture;
      final needsTexture =
          target == null ||
          target.requestedHandleType != frame.handleType ||
          _previewWidth != frame.width ||
          _previewHeight != frame.height;
      if (needsTexture) {
        if (target != null) await _platformHost.disposeTexture(target);
        target = await _platformHost.createTexture(
          handleType: frame.handleType,
          width: frame.width,
          height: frame.height,
        );
        _previewTexture = target;
        _previewWidth = frame.width;
        _previewHeight = frame.height;
      }
      await _platformHost.present(target, frame);
      return DigitorWorkspacePreviewState(
        generation: frame.generation,
        timestampUs: frame.timestampUs,
        width: frame.width,
        height: frame.height,
        backend: frame.backend,
        textureId: target.textureId,
      );
    } finally {
      if (productionTimelineConfigured) {
        _productionTimeline!.previewConsumed(frame.generation);
      } else {
        _productionSession!.previewConsumed(frame.generation);
      }
    }
  }

  void previewConsumed([int? generation]) {
    _ensureProductionReady();
    if (productionTimelineConfigured) {
      _productionTimeline!.previewConsumed(generation);
    } else {
      _productionSession!.previewConsumed(generation);
    }
  }

  void exportMedia({
    required String path,
    required int firstFrame,
    required int lastFrame,
    required int width,
    required int height,
    DigitorExportFormat format = DigitorExportFormat.mp4,
    DigitorVideoCodec codec = DigitorVideoCodec.h264,
    void Function(DigitorExportProgress progress)? onProgress,
  }) {
    _ensureProductionReady();
    final snapshotIdentity = ++_exportSnapshotIdentity;
    if (productionTimelineConfigured) {
      if (_productionTimelineRevision <= 0) {
        throw StateError('Native production timeline revision is unavailable.');
      }
      _productionTimeline!.export(
        path: path,
        firstFrame: firstFrame,
        lastFrame: lastFrame,
        width: width,
        height: height,
        snapshotIdentity: snapshotIdentity,
        timelineRevision: _productionTimelineRevision,
        renderRevision: _graph.graphRevision,
        nodeGraphRevision: _graph.graphRevision,
        colorPipelineRevision: _graph.parameterRevision,
        audioRevision: _audioRevision,
        graphRecipeIdentity: _graph.recipeIdentity,
        fpsNum: _projectFpsNum,
        fpsDen: _projectFpsDen,
        format: format,
        codec: codec,
        onProgress: onProgress,
      );
      return;
    }

    final sourceFrameDurationUs =
        _media?.firstFrame.duration.inMicroseconds ?? 0;
    final fpsNum = sourceFrameDurationUs > 0 ? 1000000 : 30;
    final fpsDen = sourceFrameDurationUs > 0 ? sourceFrameDurationUs : 1;
    _productionSession!.export(
      path: path,
      firstFrame: firstFrame,
      lastFrame: lastFrame,
      width: width,
      height: height,
      snapshotIdentity: snapshotIdentity,
      timelineRevision: _timelineRevision,
      renderRevision: _graph.graphRevision,
      nodeGraphRevision: _graph.graphRevision,
      colorPipelineRevision: _graph.parameterRevision,
      audioRevision: _audioRevision,
      graphRecipeIdentity: _graph.recipeIdentity,
      fpsNum: fpsNum,
      fpsDen: fpsDen,
      format: format,
      codec: codec,
      onProgress: onProgress,
    );
  }

  void cancelExport() {
    _ensureProductionReady();
    if (productionTimelineConfigured) {
      _productionTimeline!.cancel();
    } else {
      _productionSession!.cancel();
    }
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
    _audioRevision += 1;
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
    final isInputEndpoint = nativeSelection == endpoints.input;
    final isOutputEndpoint = nativeSelection == endpoints.output;
    if (isInputEndpoint || isOutputEndpoint) {
      _selectedNode = null;
    } else {
      _selectedNode = nativeSelection;
    }
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

  void _ensureProductionReady() {
    _ensureOpen();
    if (!productionReady) {
      throw StateError(
        'The native production host is not registered or no media/timeline is open.',
      );
    }
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

  Future<void> releaseProductionSession() async {
    if (_closed) return;
    _timeline.detachMedia();
    final productionTimeline = _productionTimeline;
    _productionTimeline = null;
    productionTimeline?.close();
    _productionTimelineRevision = 0;
    _productionSession?.dispose();
    _productionSession = null;
    final previewTexture = _previewTexture;
    _previewTexture = null;
    if (previewTexture != null) {
      await _platformHost.disposeTexture(previewTexture);
    }
  }

  Future<void> close() async {
    if (_closed) return;
    await releaseProductionSession();
    _closed = true;
    _timeline.dispose();
    _mediaPipeline.close();
    _graph.dispose();
    await _platformHost.close();
    await _engine.close();
  }
}
