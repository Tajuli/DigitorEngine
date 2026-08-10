import 'dart:async';

import 'package:flutter/foundation.dart';

import 'editor_workspace.dart';
import 'engine.dart';
import 'node_graph.dart';
import 'platform_host.dart';

@immutable
final class DigitorEditorState {
  const DigitorEditorState({
    this.mediaPath,
    this.textureId,
    this.previewGeneration = 0,
    this.previewTimestampUs = 0,
    this.previewWidth = 0,
    this.previewHeight = 0,
    this.selectedNode,
    this.graphRevision = 0,
    this.parameterRevision = 0,
    this.exportProgress,
    this.busy = false,
    this.exporting = false,
    this.error,
  });

  final String? mediaPath;
  final int? textureId;
  final int previewGeneration;
  final int previewTimestampUs;
  final int previewWidth;
  final int previewHeight;
  final int? selectedNode;
  final int graphRevision;
  final int parameterRevision;
  final DigitorExportProgress? exportProgress;
  final bool busy;
  final bool exporting;
  final Object? error;

  bool get hasMedia => mediaPath != null;
  bool get hasPreview => textureId != null;

  DigitorEditorState copyWith({
    String? mediaPath,
    bool clearMediaPath = false,
    int? textureId,
    bool clearTextureId = false,
    int? previewGeneration,
    int? previewTimestampUs,
    int? previewWidth,
    int? previewHeight,
    int? selectedNode,
    bool clearSelectedNode = false,
    int? graphRevision,
    int? parameterRevision,
    DigitorExportProgress? exportProgress,
    bool clearExportProgress = false,
    bool? busy,
    bool? exporting,
    Object? error,
    bool clearError = false,
  }) {
    return DigitorEditorState(
      mediaPath: clearMediaPath ? null : (mediaPath ?? this.mediaPath),
      textureId: clearTextureId ? null : (textureId ?? this.textureId),
      previewGeneration: previewGeneration ?? this.previewGeneration,
      previewTimestampUs: previewTimestampUs ?? this.previewTimestampUs,
      previewWidth: previewWidth ?? this.previewWidth,
      previewHeight: previewHeight ?? this.previewHeight,
      selectedNode: clearSelectedNode
          ? null
          : (selectedNode ?? this.selectedNode),
      graphRevision: graphRevision ?? this.graphRevision,
      parameterRevision: parameterRevision ?? this.parameterRevision,
      exportProgress: clearExportProgress
          ? null
          : (exportProgress ?? this.exportProgress),
      busy: busy ?? this.busy,
      exporting: exporting ?? this.exporting,
      error: clearError ? null : (error ?? this.error),
    );
  }
}

/// Flutter-friendly controller over [DigitorEditorWorkspace].
///
/// UI code listens to immutable [state] and calls typed commands. Decoder,
/// renderer, production-session ownership, graph revision binding, texture
/// ownership and native FFI handles stay inside DigitorEngine.
final class DigitorEditorController extends ChangeNotifier {
  DigitorEditorController._(this._workspace)
    : _state = DigitorEditorState(
        selectedNode: _workspace.selectedNode,
        graphRevision: _workspace.graphRevision,
        parameterRevision: _workspace.parameterRevision,
      );

  static Future<DigitorEditorController> create({
    DigitorBackend preferredBackend = DigitorBackend.automatic,
    bool allowCpuFallback = true,
    int sampleRate = 48000,
    int channels = 2,
  }) async {
    final workspace = await DigitorEditorWorkspace.create(
      preferredBackend: preferredBackend,
      allowCpuFallback: allowCpuFallback,
      sampleRate: sampleRate,
      channels: channels,
    );
    return DigitorEditorController._(workspace);
  }

  final DigitorEditorWorkspace _workspace;
  DigitorEditorState _state;
  bool _disposed = false;

  DigitorEditorState get state => _state;
  DigitorRendererInfo get renderer => _workspace.renderer;
  DigitorFlutterHostCapabilities? get hostCapabilities =>
      _workspace.hostCapabilities;
  bool get productionHostRegistered => _workspace.productionHostRegistered;
  bool get productionReady => _workspace.productionReady;
  String get recipeIdentity => _workspace.recipeIdentity;

  Future<void> openMedia(String path) async {
    _ensureAlive();
    await _guard(() async {
      _workspace.openMedia(path);
      _syncState(mediaPath: path, clearTexture: true);
    });
  }

  Future<DigitorWorkspacePreviewState> renderPreview({
    required int timestampUs,
    required int width,
    required int height,
  }) async {
    _ensureAlive();
    late DigitorWorkspacePreviewState preview;
    await _guard(() async {
      preview = await _workspace.presentPreview(
        timestampUs: timestampUs,
        width: width,
        height: height,
      );
      _syncState(
        textureId: preview.textureId,
        previewGeneration: preview.generation,
        previewTimestampUs: preview.timestampUs,
        previewWidth: preview.width,
        previewHeight: preview.height,
      );
    });
    return preview;
  }

  Future<void> exportMedia({
    required String path,
    required int firstFrame,
    required int lastFrame,
    required int width,
    required int height,
    DigitorExportFormat format = DigitorExportFormat.mp4,
    DigitorVideoCodec codec = DigitorVideoCodec.h264,
  }) async {
    _ensureAlive();
    _setState(
      _state.copyWith(
        exporting: true,
        clearExportProgress: true,
        clearError: true,
      ),
    );
    try {
      _workspace.exportMedia(
        path: path,
        firstFrame: firstFrame,
        lastFrame: lastFrame,
        width: width,
        height: height,
        format: format,
        codec: codec,
        onProgress: (progress) {
          if (!_disposed) _setState(_state.copyWith(exportProgress: progress));
        },
      );
    } catch (error) {
      if (!_disposed) _setState(_state.copyWith(error: error));
      rethrow;
    } finally {
      if (!_disposed) _setState(_state.copyWith(exporting: false));
    }
  }

  void cancelExport() => _command(_workspace.cancelExport);
  void play() => _command(_workspace.play);
  void pause() => _command(_workspace.pause);
  void stop() => _command(_workspace.stop);
  void seek(int positionUs) => _command(() => _workspace.seek(positionUs));

  void selectNode(int node) => _graphCommand(() => _workspace.selectNode(node));

  int addSerialNode({String name = 'Serial Node'}) {
    _ensureAlive();
    final node = _workspace.addSerialNode(name: name);
    _syncState();
    return node;
  }

  DigitorParallelNodes addParallelNodes() {
    _ensureAlive();
    final nodes = _workspace.addParallelNodes();
    _syncState();
    return nodes;
  }

  void removeSelectedNode() => _graphCommand(_workspace.removeSelectedNode);
  void clearSelectedOperations() =>
      _graphCommand(_workspace.clearSelectedOperations);
  void moveSelectedNode(double x, double y) =>
      _graphCommand(() => _workspace.moveSelectedNode(x, y));
  void setSelectedEnabled(bool enabled) =>
      _graphCommand(() => _workspace.setSelectedEnabled(enabled));
  void setSelectedBypassed(bool bypassed) =>
      _graphCommand(() => _workspace.setSelectedBypassed(bypassed));
  void convertSelectedToParallel() =>
      _graphCommand(_workspace.convertSelectedToParallel);
  void connectNodes(int source, int destination) =>
      _graphCommand(() => _workspace.connectNodes(source, destination));
  void disconnectNodes(int source, int destination) =>
      _graphCommand(() => _workspace.disconnectNodes(source, destination));

  void applyCorrection(DigitorCorrection value) =>
      _graphCommand(() => _workspace.addCorrection(value));
  void applyPrimaryWheels(DigitorPrimaryWheels value) =>
      _graphCommand(() => _workspace.addPrimaryWheels(value));
  void applyLogWheels(DigitorLogWheels value) =>
      _graphCommand(() => _workspace.addLogWheels(value));
  void applyRgbCurves(DigitorRgbCurves value) =>
      _graphCommand(() => _workspace.addRgbCurves(value));
  void applyHslQualifier(DigitorHslQualifier value) =>
      _graphCommand(() => _workspace.addHslQualifier(value));
  void applyEffect(DigitorNodeEffect value) =>
      _graphCommand(() => _workspace.addEffect(value));
  void applyPowerWindow(DigitorPowerWindow value) =>
      _graphCommand(() => _workspace.addPowerWindow(value));
  void applyLut1d(List<DigitorLutColor> values) =>
      _graphCommand(() => _workspace.addLut1d(values));
  void applyLut3d(
    int edgeSize,
    List<DigitorLutColor> values, {
    DigitorLutInterpolation interpolation = DigitorLutInterpolation.tetrahedral,
  }) => _graphCommand(
    () => _workspace.addLut3d(edgeSize, values, interpolation: interpolation),
  );

  void clearError() {
    _ensureAlive();
    _setState(_state.copyWith(clearError: true));
  }

  void _command(VoidCallback command) {
    _ensureAlive();
    try {
      command();
      _setState(_state.copyWith(clearError: true));
    } catch (error) {
      _setState(_state.copyWith(error: error));
      rethrow;
    }
  }

  void _graphCommand(VoidCallback command) {
    _command(() {
      command();
      _syncState();
    });
  }

  Future<void> _guard(Future<void> Function() operation) async {
    _setState(_state.copyWith(busy: true, clearError: true));
    try {
      await operation();
    } catch (error) {
      _setState(_state.copyWith(error: error));
      rethrow;
    } finally {
      if (!_disposed) _setState(_state.copyWith(busy: false));
    }
  }

  void _syncState({
    String? mediaPath,
    int? textureId,
    bool clearTexture = false,
    int? previewGeneration,
    int? previewTimestampUs,
    int? previewWidth,
    int? previewHeight,
  }) {
    _setState(
      _state.copyWith(
        mediaPath: mediaPath,
        textureId: textureId,
        clearTextureId: clearTexture,
        previewGeneration: previewGeneration,
        previewTimestampUs: previewTimestampUs,
        previewWidth: previewWidth,
        previewHeight: previewHeight,
        selectedNode: _workspace.selectedNode,
        clearSelectedNode: _workspace.selectedNode == null,
        graphRevision: _workspace.graphRevision,
        parameterRevision: _workspace.parameterRevision,
        clearError: true,
      ),
    );
  }

  void _setState(DigitorEditorState next) {
    if (_disposed) return;
    _state = next;
    notifyListeners();
  }

  void _ensureAlive() {
    if (_disposed) throw StateError('DigitorEditorController is disposed.');
  }

  Future<void> close() async {
    if (_disposed) return;
    _disposed = true;
    try {
      await _workspace.close();
    } finally {
      super.dispose();
    }
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    unawaited(_workspace.close());
    super.dispose();
  }
}
