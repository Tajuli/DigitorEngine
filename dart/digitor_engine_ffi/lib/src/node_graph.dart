import 'dart:convert';
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'native_node_graph_api.dart';

final class DigitorNodeGraphException implements Exception {
  const DigitorNodeGraphException(this.operation, this.resultCode);
  final String operation;
  final int resultCode;
  @override
  String toString() =>
      'DigitorNodeGraphException(operation: $operation, result: $resultCode)';
}

final class DigitorNodeGraphEndpoints {
  const DigitorNodeGraphEndpoints(this.input, this.output);
  final int input;
  final int output;
}

final class DigitorParallelNodes {
  const DigitorParallelNodes(this.first, this.second);
  final int first;
  final int second;
}

final class DigitorRgb {
  const DigitorRgb(this.r, this.g, this.b);
  const DigitorRgb.neutral() : r = 0, g = 0, b = 0;
  final double r;
  final double g;
  final double b;
}

final class DigitorPrimaryWheel {
  const DigitorPrimaryWheel({
    this.rgb = const DigitorRgb.neutral(),
    this.master = 0,
    this.enabled = true,
  });
  final DigitorRgb rgb;
  final double master;
  final bool enabled;
}

final class DigitorPrimaryWheels {
  const DigitorPrimaryWheels({
    this.lift = const DigitorPrimaryWheel(),
    this.gamma = const DigitorPrimaryWheel(),
    this.gain = const DigitorPrimaryWheel(),
    this.offset = const DigitorPrimaryWheel(),
  });
  final DigitorPrimaryWheel lift;
  final DigitorPrimaryWheel gamma;
  final DigitorPrimaryWheel gain;
  final DigitorPrimaryWheel offset;
}

final class DigitorLogWheel {
  const DigitorLogWheel({
    this.rgb = const DigitorRgb.neutral(),
    this.master = 0,
    this.enabled = true,
  });
  final DigitorRgb rgb;
  final double master;
  final bool enabled;
}

final class DigitorLogWheels {
  const DigitorLogWheels({
    this.shadows = const DigitorLogWheel(),
    this.midtones = const DigitorLogWheel(),
    this.highlights = const DigitorLogWheel(),
    this.global = const DigitorLogWheel(),
    this.shadowPivot = 0.33,
    this.highlightPivot = 0.67,
    this.transitionWidth = 0.1,
  });
  final DigitorLogWheel shadows;
  final DigitorLogWheel midtones;
  final DigitorLogWheel highlights;
  final DigitorLogWheel global;
  final double shadowPivot;
  final double highlightPivot;
  final double transitionWidth;
}

final class DigitorCurvePoint {
  const DigitorCurvePoint(this.x, this.y);
  final double x;
  final double y;
}

final class DigitorCurveChannel {
  const DigitorCurveChannel({
    this.points = const <DigitorCurvePoint>[
      DigitorCurvePoint(0, 0),
      DigitorCurvePoint(1, 1),
    ],
    this.enabled = true,
  });
  final List<DigitorCurvePoint> points;
  final bool enabled;
}

final class DigitorRgbCurves {
  const DigitorRgbCurves({
    this.master = const DigitorCurveChannel(),
    this.red = const DigitorCurveChannel(),
    this.green = const DigitorCurveChannel(),
    this.blue = const DigitorCurveChannel(),
    this.lutSize = 1024,
  });
  final DigitorCurveChannel master;
  final DigitorCurveChannel red;
  final DigitorCurveChannel green;
  final DigitorCurveChannel blue;
  final int lutSize;
}

final class DigitorQualifierRange {
  const DigitorQualifierRange({
    this.low = 0,
    this.high = 1,
    this.softness = 0,
  });
  final double low;
  final double high;
  final double softness;
}

final class DigitorHslQualifier {
  const DigitorHslQualifier({
    this.hue = const DigitorQualifierRange(),
    this.saturation = const DigitorQualifierRange(),
    this.luminance = const DigitorQualifierRange(),
    this.blur = 0,
    this.denoise = 0,
    this.cleanBlack = 0,
    this.cleanWhite = 0,
    this.invert = false,
    this.matteOutput = false,
  });
  final DigitorQualifierRange hue;
  final DigitorQualifierRange saturation;
  final DigitorQualifierRange luminance;
  final double blur;
  final double denoise;
  final double cleanBlack;
  final double cleanWhite;
  final bool invert;
  final bool matteOutput;
}

final class DigitorCorrection {
  const DigitorCorrection({
    this.exposure = 0,
    this.contrast = 0,
    this.saturation = 0,
    this.temperature = 0,
    this.tint = 0,
    this.highlights = 0,
    this.shadows = 0,
    this.hue = 0,
    this.colorBoost = 0,
  });
  final double exposure;
  final double contrast;
  final double saturation;
  final double temperature;
  final double tint;
  final double highlights;
  final double shadows;
  final double hue;
  final double colorBoost;
}

enum DigitorLutInterpolation { nearest, linear, tetrahedral }

final class DigitorLutColor {
  const DigitorLutColor(this.r, this.g, this.b, [this.a = 1]);
  final double r;
  final double g;
  final double b;
  final double a;
}

enum DigitorNodeEffectType {
  blur,
  sharpen,
  glow,
  lensDistortion,
  noise,
  filmGrain,
  chromaticAberration,
  vignette,
  motionBlur,
}

final class DigitorNodeEffect {
  const DigitorNodeEffect({
    required this.type,
    this.amount = 0,
    this.radius = 0,
    this.angle = 0,
    this.seed = 0,
  });
  final DigitorNodeEffectType type;
  final double amount;
  final double radius;
  final double angle;
  final int seed;
}

enum DigitorPowerWindowShape { rectangle, ellipse, linearGradient }

final class DigitorPowerWindow {
  const DigitorPowerWindow({
    this.shape = DigitorPowerWindowShape.ellipse,
    this.centerX = 0.5,
    this.centerY = 0.5,
    this.width = 1,
    this.height = 1,
    this.rotation = 0,
    this.feather = 0.1,
    this.opacity = 1,
    this.invert = false,
  });
  final DigitorPowerWindowShape shape;
  final double centerX;
  final double centerY;
  final double width;
  final double height;
  final double rotation;
  final double feather;
  final double opacity;
  final bool invert;
}

/// Caller-owned production node graph shared by preview and export.
///
/// [graphRevision] changes for every render-affecting graph mutation.
/// [parameterRevision] additionally changes when a color/effect operation is
/// changed. A production session pins both values before rendering.
final class DigitorNodeGraph {
  DigitorNodeGraph._(this._handle);

  factory DigitorNodeGraph.create() {
    final out = calloc<Pointer<DigitorNodeGraphNative>>();
    try {
      _check('create', digitorNodeGraphCreate(out));
      if (out.value == nullptr) {
        throw const DigitorNodeGraphException('create', 100);
      }
      return DigitorNodeGraph._(out.value);
    } finally {
      calloc.free(out);
    }
  }

  Pointer<DigitorNodeGraphNative> _handle;
  int _graphRevision = 1;
  int _parameterRevision = 1;
  int _productionBindings = 0;
  bool _disposed = false;

  int get graphRevision => _graphRevision;
  int get parameterRevision => _parameterRevision;

  /// Native handle used by the package's production-session bridge.
  Pointer<DigitorNodeGraphNative> get nativeHandle {
    _ensureAlive();
    return _handle;
  }

  DigitorNodeGraphEndpoints get endpoints {
    _ensureAlive();
    final input = calloc<Uint64>();
    final output = calloc<Uint64>();
    try {
      _check('getEndpoints', digitorNodeGraphGetEndpoints(_handle, input, output));
      return DigitorNodeGraphEndpoints(input.value, output.value);
    } finally {
      calloc.free(input);
      calloc.free(output);
    }
  }

  int get selectedNode {
    _ensureAlive();
    final out = calloc<Uint64>();
    try {
      _check('getSelected', digitorNodeGraphGetSelected(_handle, out));
      return out.value;
    } finally {
      calloc.free(out);
    }
  }

  void select(int node) {
    _ensureAlive();
    _check('select', digitorNodeGraphSelect(_handle, node));
  }

  int addSerialAfter(int after, {String name = 'Serial Node'}) {
    _ensureAlive();
    final nativeName = name.toNativeUtf8();
    final out = calloc<Uint64>();
    try {
      _check('addSerialAfter', digitorNodeGraphAddSerialAfter(_handle, after, nativeName, out));
      _touchGraph();
      return out.value;
    } finally {
      calloc.free(nativeName);
      calloc.free(out);
    }
  }

  DigitorParallelNodes addParallelAfter(
    int after, {
    String firstName = 'Parallel A',
    String secondName = 'Parallel B',
  }) {
    _ensureAlive();
    final firstNameNative = firstName.toNativeUtf8();
    final secondNameNative = secondName.toNativeUtf8();
    final first = calloc<Uint64>();
    final second = calloc<Uint64>();
    try {
      _check(
        'addParallelAfter',
        digitorNodeGraphAddParallelAfter(
          _handle,
          after,
          firstNameNative,
          secondNameNative,
          first,
          second,
        ),
      );
      _touchGraph();
      return DigitorParallelNodes(first.value, second.value);
    } finally {
      calloc.free(firstNameNative);
      calloc.free(secondNameNative);
      calloc.free(first);
      calloc.free(second);
    }
  }

  int convertToParallel(int node, {String branchName = 'Parallel Node'}) {
    _ensureAlive();
    final name = branchName.toNativeUtf8();
    final out = calloc<Uint64>();
    try {
      _check('convertToParallel', digitorNodeGraphConvertToParallel(_handle, node, name, out));
      _touchGraph();
      return out.value;
    } finally {
      calloc.free(name);
      calloc.free(out);
    }
  }

  void remove(int node) => _graphMutation('remove', () => digitorNodeGraphRemove(_handle, node));
  void connect(int source, int destination) => _graphMutation('connect', () => digitorNodeGraphConnect(_handle, source, destination));
  void disconnect(int source, int destination) => _graphMutation('disconnect', () => digitorNodeGraphDisconnect(_handle, source, destination));

  void setPosition(int node, double x, double y) {
    _ensureAlive();
    final value = calloc<DigitorNodePositionNative>();
    try {
      value.ref
        ..x = x
        ..y = y;
      _check('setPosition', digitorNodeGraphSetPosition(_handle, node, value.ref));
      _touchGraph();
    } finally {
      calloc.free(value);
    }
  }

  void setEnabled(int node, bool enabled) => _graphMutation('setEnabled', () => digitorNodeGraphSetEnabled(_handle, node, enabled ? 1 : 0));
  void setBypassed(int node, bool bypassed) => _graphMutation('setBypassed', () => digitorNodeGraphSetBypassed(_handle, node, bypassed ? 1 : 0));

  void clearOperations(int node) {
    _parameterMutation('clearOperations', () => digitorNodeGraphClearOperations(_handle, node));
  }

  void addPrimaryWheels(DigitorPrimaryWheels controls) {
    _ensureAlive();
    final native = calloc<DigitorPrimaryWheelsControlsNative>();
    try {
      _writePrimaryWheel(native.ref.lift, controls.lift);
      native.ref
        ..liftMaster = controls.lift.master
        ..liftEnabled = controls.lift.enabled ? 1 : 0;
      _writePrimaryWheel(native.ref.gamma, controls.gamma);
      native.ref
        ..gammaMaster = controls.gamma.master
        ..gammaEnabled = controls.gamma.enabled ? 1 : 0;
      _writePrimaryWheel(native.ref.gain, controls.gain);
      native.ref
        ..gainMaster = controls.gain.master
        ..gainEnabled = controls.gain.enabled ? 1 : 0;
      _writePrimaryWheel(native.ref.offset, controls.offset);
      native.ref
        ..offsetMaster = controls.offset.master
        ..offsetEnabled = controls.offset.enabled ? 1 : 0;
      _check('addPrimaryWheels', digitorNodeGraphAddPrimaryWheels(_handle, native));
      _touchParameters();
    } finally {
      calloc.free(native);
    }
  }

  void addLogWheels(DigitorLogWheels controls) {
    _ensureAlive();
    final native = calloc<DigitorLogWheelsControlsNative>();
    try {
      _writeLogWheel(native.ref.shadows, controls.shadows);
      _writeLogWheel(native.ref.midtones, controls.midtones);
      _writeLogWheel(native.ref.highlights, controls.highlights);
      _writeLogWheel(native.ref.global, controls.global);
      native.ref
        ..shadowPivot = controls.shadowPivot
        ..highlightPivot = controls.highlightPivot
        ..transitionWidth = controls.transitionWidth;
      _check('addLogWheels', digitorNodeGraphAddLogWheels(_handle, native));
      _touchParameters();
    } finally {
      calloc.free(native);
    }
  }

  void addRgbCurves(DigitorRgbCurves curves) {
    _ensureAlive();
    final controls = calloc<DigitorRgbCurvesControlsNative>();
    final allocations = <Pointer<DigitorCurvePointNative>>[];
    try {
      void writeChannel(DigitorCurveChannelNative native, DigitorCurveChannel channel) {
        if (channel.points.length < 2) {
          throw ArgumentError('Each RGB curve channel needs at least two points.');
        }
        final points = calloc<DigitorCurvePointNative>(channel.points.length);
        allocations.add(points);
        for (var i = 0; i < channel.points.length; i++) {
          points[i]
            ..x = channel.points[i].x
            ..y = channel.points[i].y;
        }
        native
          ..points = points
          ..pointCount = channel.points.length
          ..enabled = channel.enabled ? 1 : 0;
      }

      writeChannel(controls.ref.master, curves.master);
      writeChannel(controls.ref.red, curves.red);
      writeChannel(controls.ref.green, curves.green);
      writeChannel(controls.ref.blue, curves.blue);
      controls.ref.lutSize = curves.lutSize;
      _check('addRgbCurves', digitorNodeGraphAddRgbCurves(_handle, controls));
      _touchParameters();
    } finally {
      for (final allocation in allocations) {
        calloc.free(allocation);
      }
      calloc.free(controls);
    }
  }

  void addHslQualifier(DigitorHslQualifier qualifier) {
    _ensureAlive();
    final native = calloc<DigitorHslQualifierControlsNative>();
    try {
      _writeQualifierRange(native.ref.hue, qualifier.hue);
      _writeQualifierRange(native.ref.saturation, qualifier.saturation);
      _writeQualifierRange(native.ref.luminance, qualifier.luminance);
      native.ref
        ..blur = qualifier.blur
        ..denoise = qualifier.denoise
        ..cleanBlack = qualifier.cleanBlack
        ..cleanWhite = qualifier.cleanWhite
        ..invert = qualifier.invert ? 1 : 0
        ..matteOutput = qualifier.matteOutput ? 1 : 0;
      _check('addHslQualifier', digitorNodeGraphAddHslQualifier(_handle, native));
      _touchParameters();
    } finally {
      calloc.free(native);
    }
  }

  void addCorrection(DigitorCorrection correction) {
    _ensureAlive();
    final native = calloc<DigitorCorrectionControlsNative>();
    try {
      native.ref
        ..exposure = correction.exposure
        ..contrast = correction.contrast
        ..saturation = correction.saturation
        ..temperature = correction.temperature
        ..tint = correction.tint
        ..highlights = correction.highlights
        ..shadows = correction.shadows
        ..hue = correction.hue
        ..colorBoost = correction.colorBoost;
      _check('addCorrection', digitorNodeGraphAddCorrection(_handle, native));
      _touchParameters();
    } finally {
      calloc.free(native);
    }
  }

  void addLut1d(
    List<DigitorLutColor> values, {
    DigitorLutInterpolation interpolation = DigitorLutInterpolation.linear,
  }) {
    if (values.length < 2) throw ArgumentError('A 1D LUT needs at least two values.');
    _ensureAlive();
    final colors = _allocateLutColors(values);
    final controls = calloc<DigitorLut1DControlsNative>();
    try {
      controls.ref
        ..values = colors
        ..valueCount = values.length
        ..interpolation = interpolation.index;
      _check('addLut1d', digitorNodeGraphAddLut1d(_handle, controls));
      _touchParameters();
    } finally {
      calloc.free(colors);
      calloc.free(controls);
    }
  }

  void addLut3d(
    int size,
    List<DigitorLutColor> values, {
    DigitorLutInterpolation interpolation = DigitorLutInterpolation.tetrahedral,
  }) {
    if (size < 2 || values.length != size * size * size) {
      throw ArgumentError('3D LUT value count must equal size³ and size must be >= 2.');
    }
    _ensureAlive();
    final colors = _allocateLutColors(values);
    final controls = calloc<DigitorLut3DControlsNative>();
    try {
      controls.ref
        ..size = size
        ..values = colors
        ..valueCount = values.length
        ..interpolation = interpolation.index;
      _check('addLut3d', digitorNodeGraphAddLut3d(_handle, controls));
      _touchParameters();
    } finally {
      calloc.free(colors);
      calloc.free(controls);
    }
  }

  void addEffect(DigitorNodeEffect effect) {
    _ensureAlive();
    final native = calloc<DigitorNodeEffectSettingsNative>();
    try {
      native.ref
        ..type = effect.type.index
        ..amount = effect.amount
        ..radius = effect.radius
        ..angle = effect.angle
        ..seed = effect.seed;
      _check('addEffect', digitorNodeGraphAddEffect(_handle, native));
      _touchParameters();
    } finally {
      calloc.free(native);
    }
  }

  void addPowerWindow(DigitorPowerWindow window) {
    _ensureAlive();
    final native = calloc<DigitorPowerWindowSettingsNative>();
    try {
      native.ref
        ..shape = window.shape.index
        ..centerX = window.centerX
        ..centerY = window.centerY
        ..width = window.width
        ..height = window.height
        ..rotation = window.rotation
        ..feather = window.feather
        ..opacity = window.opacity
        ..invert = window.invert ? 1 : 0;
      _check('addPowerWindow', digitorNodeGraphAddPowerWindow(_handle, native));
      _touchParameters();
    } finally {
      calloc.free(native);
    }
  }

  String get recipeIdentity => _readNativeText('recipeIdentity', digitorNodeGraphRecipeIdentity);
  String get json => _readNativeText('toJson', digitorNodeGraphToJson);

  /// Used by [DigitorProductionSession] to keep the native graph alive.
  void retainForProductionSession() {
    _ensureAlive();
    _productionBindings++;
  }

  /// Paired with [retainForProductionSession].
  void releaseFromProductionSession() {
    if (_productionBindings > 0) _productionBindings--;
  }

  void dispose() {
    if (_disposed) return;
    if (_productionBindings != 0) {
      throw StateError('Dispose production sessions before disposing their node graph.');
    }
    _check('destroy', digitorNodeGraphDestroy(_handle));
    _handle = nullptr;
    _disposed = true;
  }

  void _graphMutation(String operation, int Function() action) {
    _ensureAlive();
    _check(operation, action());
    _touchGraph();
  }

  void _parameterMutation(String operation, int Function() action) {
    _ensureAlive();
    _check(operation, action());
    _touchParameters();
  }

  void _touchGraph() => _graphRevision++;
  void _touchParameters() {
    _graphRevision++;
    _parameterRevision++;
  }

  String _readNativeText(
    String operation,
    int Function(Pointer<DigitorNodeGraphNative>, Pointer<Uint8>, int, Pointer<Uint64>) call,
  ) {
    _ensureAlive();
    final required = calloc<Uint64>();
    try {
      _check(operation, call(_handle, nullptr, 0, required));
      if (required.value == 0) return '';
      final buffer = calloc<Uint8>(required.value);
      try {
        _check(operation, call(_handle, buffer, required.value, required));
        final bytes = buffer.asTypedList(required.value - 1);
        return utf8.decode(bytes, allowMalformed: false);
      } finally {
        calloc.free(buffer);
      }
    } finally {
      calloc.free(required);
    }
  }

  void _ensureAlive() {
    if (_disposed || _handle == nullptr) {
      throw StateError('DigitorNodeGraph is disposed.');
    }
  }

  static Pointer<DigitorLutColorNative> _allocateLutColors(List<DigitorLutColor> values) {
    final out = calloc<DigitorLutColorNative>(values.length);
    for (var i = 0; i < values.length; i++) {
      out[i]
        ..r = values[i].r
        ..g = values[i].g
        ..b = values[i].b
        ..a = values[i].a;
    }
    return out;
  }

  static void _writePrimaryWheel(DigitorRgbNative native, DigitorPrimaryWheel wheel) {
    native
      ..r = wheel.rgb.r
      ..g = wheel.rgb.g
      ..b = wheel.rgb.b;
  }

  static void _writeLogWheel(DigitorLogWheelControlNative native, DigitorLogWheel wheel) {
    native
      ..master = wheel.master
      ..enabled = wheel.enabled ? 1 : 0;
    native.rgb
      ..r = wheel.rgb.r
      ..g = wheel.rgb.g
      ..b = wheel.rgb.b;
  }

  static void _writeQualifierRange(DigitorQualifierRangeNative native, DigitorQualifierRange range) {
    native
      ..low = range.low
      ..high = range.high
      ..softness = range.softness;
  }

  static void _check(String operation, int result) {
    if (result != 0) throw DigitorNodeGraphException(operation, result);
  }
}
