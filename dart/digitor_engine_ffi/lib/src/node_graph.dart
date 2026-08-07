import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'library_loader.dart';
import 'native_engine.dart';

typedef DigitorNodeId = int;

final class DigitorNodePosition {
  const DigitorNodePosition(this.x, this.y);
  final double x;
  final double y;
}

final class DigitorRgbValue {
  const DigitorRgbValue(this.r, this.g, this.b);
  const DigitorRgbValue.identity() : r = 0, g = 0, b = 0;
  final double r;
  final double g;
  final double b;
}

final class DigitorPrimaryWheelValue {
  const DigitorPrimaryWheelValue({
    this.rgb = const DigitorRgbValue.identity(),
    this.master = 0,
    this.enabled = true,
  });
  final DigitorRgbValue rgb;
  final double master;
  final bool enabled;
}

final class DigitorPrimaryWheels {
  const DigitorPrimaryWheels({
    this.lift = const DigitorPrimaryWheelValue(),
    this.gamma = const DigitorPrimaryWheelValue(),
    this.gain = const DigitorPrimaryWheelValue(),
    this.offset = const DigitorPrimaryWheelValue(),
  });
  final DigitorPrimaryWheelValue lift;
  final DigitorPrimaryWheelValue gamma;
  final DigitorPrimaryWheelValue gain;
  final DigitorPrimaryWheelValue offset;
}

final class DigitorLogWheelValue {
  const DigitorLogWheelValue({
    this.rgb = const DigitorRgbValue.identity(),
    this.master = 0,
    this.enabled = true,
  });
  final DigitorRgbValue rgb;
  final double master;
  final bool enabled;
}

final class DigitorLogWheels {
  const DigitorLogWheels({
    this.shadows = const DigitorLogWheelValue(),
    this.midtones = const DigitorLogWheelValue(),
    this.highlights = const DigitorLogWheelValue(),
    this.global = const DigitorLogWheelValue(),
    this.shadowPivot = 0.33,
    this.highlightPivot = 0.67,
    this.transitionWidth = 0.1,
  });
  final DigitorLogWheelValue shadows;
  final DigitorLogWheelValue midtones;
  final DigitorLogWheelValue highlights;
  final DigitorLogWheelValue global;
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
    this.points = const [DigitorCurvePoint(0, 0), DigitorCurvePoint(1, 1)],
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
  const DigitorQualifierRange({this.low = 0, this.high = 1, this.softness = 0});
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

final class DigitorLutColor {
  const DigitorLutColor(this.r, this.g, this.b, [this.a = 1]);
  final double r;
  final double g;
  final double b;
  final double a;
}

enum DigitorLutInterpolation { nearest, linear, tetrahedral }

final class _NativeNodePosition extends Struct {
  @Float()
  external double x;
  @Float()
  external double y;
}

final class _NativeRgb extends Struct {
  @Float()
  external double r;
  @Float()
  external double g;
  @Float()
  external double b;
}

final class _NativePrimaryWheels extends Struct {
  external _NativeRgb lift;
  @Float()
  external double liftMaster;
  @Uint8()
  external int liftEnabled;
  external _NativeRgb gamma;
  @Float()
  external double gammaMaster;
  @Uint8()
  external int gammaEnabled;
  external _NativeRgb gain;
  @Float()
  external double gainMaster;
  @Uint8()
  external int gainEnabled;
  external _NativeRgb offset;
  @Float()
  external double offsetMaster;
  @Uint8()
  external int offsetEnabled;
}

final class _NativeLogWheel extends Struct {
  external _NativeRgb rgb;
  @Float()
  external double master;
  @Uint8()
  external int enabled;
}

final class _NativeLogWheels extends Struct {
  external _NativeLogWheel shadows;
  external _NativeLogWheel midtones;
  external _NativeLogWheel highlights;
  external _NativeLogWheel global;
  @Float()
  external double shadowPivot;
  @Float()
  external double highlightPivot;
  @Float()
  external double transitionWidth;
}

final class _NativeCurvePoint extends Struct {
  @Float()
  external double x;
  @Float()
  external double y;
}

final class _NativeCurveChannel extends Struct {
  external Pointer<_NativeCurvePoint> points;
  @Uint32()
  external int pointCount;
  @Uint8()
  external int enabled;
}

final class _NativeRgbCurves extends Struct {
  external _NativeCurveChannel master;
  external _NativeCurveChannel red;
  external _NativeCurveChannel green;
  external _NativeCurveChannel blue;
  @Uint32()
  external int lutSize;
}

final class _NativeQualifierRange extends Struct {
  @Float()
  external double low;
  @Float()
  external double high;
  @Float()
  external double softness;
}

final class _NativeHslQualifier extends Struct {
  external _NativeQualifierRange hue;
  external _NativeQualifierRange saturation;
  external _NativeQualifierRange luminance;
  @Float()
  external double blur;
  @Float()
  external double denoise;
  @Float()
  external double cleanBlack;
  @Float()
  external double cleanWhite;
  @Uint8()
  external int invert;
  @Uint8()
  external int matteOutput;
}

final class _NativeLutColor extends Struct {
  @Float()
  external double r;
  @Float()
  external double g;
  @Float()
  external double b;
  @Float()
  external double a;
}

final class _NativeLut1d extends Struct {
  external Pointer<_NativeLutColor> values;
  @Uint32()
  external int valueCount;
  @Int32()
  external int interpolation;
}

final class _NativeLut3d extends Struct {
  @Uint32()
  external int size;
  external Pointer<_NativeLutColor> values;
  @Uint64()
  external int valueCount;
  @Int32()
  external int interpolation;
}

final class _NativeEffect extends Struct {
  @Int32()
  external int type;
  @Float()
  external double amount;
  @Float()
  external double radius;
  @Float()
  external double angle;
  @Uint64()
  external int seed;
}

final class _NativePowerWindow extends Struct {
  @Int32()
  external int shape;
  @Float()
  external double centerX;
  @Float()
  external double centerY;
  @Float()
  external double width;
  @Float()
  external double height;
  @Float()
  external double rotation;
  @Float()
  external double feather;
  @Float()
  external double opacity;
  @Uint8()
  external int invert;
}

typedef _GraphCreateN = Int32 Function(Pointer<Pointer<Void>>);
typedef _GraphCreateD = int Function(Pointer<Pointer<Void>>);
typedef _GraphDestroyN = Int32 Function(Pointer<Void>);
typedef _GraphDestroyD = int Function(Pointer<Void>);
typedef _GetEndpointsN = Int32 Function(Pointer<Void>, Pointer<Uint64>, Pointer<Uint64>);
typedef _GetEndpointsD = int Function(Pointer<Void>, Pointer<Uint64>, Pointer<Uint64>);
typedef _NodeN = Int32 Function(Pointer<Void>, Uint64);
typedef _NodeD = int Function(Pointer<Void>, int);
typedef _NodeBoolN = Int32 Function(Pointer<Void>, Uint64, Uint8);
typedef _NodeBoolD = int Function(Pointer<Void>, int, int);
typedef _PositionN = Int32 Function(Pointer<Void>, Uint64, _NativeNodePosition);
typedef _PositionD = int Function(Pointer<Void>, int, _NativeNodePosition);
typedef _AddSerialN = Int32 Function(Pointer<Void>, Uint64, Pointer<Utf8>, Pointer<Uint64>);
typedef _AddSerialD = int Function(Pointer<Void>, int, Pointer<Utf8>, Pointer<Uint64>);
typedef _AddParallelN = Int32 Function(Pointer<Void>, Uint64, Pointer<Utf8>, Pointer<Utf8>, Pointer<Uint64>, Pointer<Uint64>);
typedef _AddParallelD = int Function(Pointer<Void>, int, Pointer<Utf8>, Pointer<Utf8>, Pointer<Uint64>, Pointer<Uint64>);
typedef _ConnectN = Int32 Function(Pointer<Void>, Uint64, Uint64);
typedef _ConnectD = int Function(Pointer<Void>, int, int);
typedef _PrimaryN = Int32 Function(Pointer<Void>, Pointer<_NativePrimaryWheels>);
typedef _PrimaryD = int Function(Pointer<Void>, Pointer<_NativePrimaryWheels>);
typedef _LogN = Int32 Function(Pointer<Void>, Pointer<_NativeLogWheels>);
typedef _LogD = int Function(Pointer<Void>, Pointer<_NativeLogWheels>);
typedef _CurvesN = Int32 Function(Pointer<Void>, Pointer<_NativeRgbCurves>);
typedef _CurvesD = int Function(Pointer<Void>, Pointer<_NativeRgbCurves>);
typedef _QualifierN = Int32 Function(Pointer<Void>, Pointer<_NativeHslQualifier>);
typedef _QualifierD = int Function(Pointer<Void>, Pointer<_NativeHslQualifier>);
typedef _Lut1dN = Int32 Function(Pointer<Void>, Pointer<_NativeLut1d>);
typedef _Lut1dD = int Function(Pointer<Void>, Pointer<_NativeLut1d>);
typedef _Lut3dN = Int32 Function(Pointer<Void>, Pointer<_NativeLut3d>);
typedef _Lut3dD = int Function(Pointer<Void>, Pointer<_NativeLut3d>);
typedef _EffectN = Int32 Function(Pointer<Void>, Pointer<_NativeEffect>);
typedef _EffectD = int Function(Pointer<Void>, Pointer<_NativeEffect>);
typedef _WindowN = Int32 Function(Pointer<Void>, Pointer<_NativePowerWindow>);
typedef _WindowD = int Function(Pointer<Void>, Pointer<_NativePowerWindow>);
typedef _TextN = Int32 Function(Pointer<Void>, Pointer<Char>, Uint64, Pointer<Uint64>);
typedef _TextD = int Function(Pointer<Void>, Pointer<Char>, int, Pointer<Uint64>);

final class DigitorNativeNodeGraph {
  DigitorNativeNodeGraph._(this._library, this._handle)
      : _destroy = _library.lookupFunction<_GraphDestroyN, _GraphDestroyD>('digitor_node_graph_destroy'),
        _getEndpoints = _library.lookupFunction<_GetEndpointsN, _GetEndpointsD>('digitor_node_graph_get_endpoints'),
        _select = _library.lookupFunction<_NodeN, _NodeD>('digitor_node_graph_select'),
        _addSerial = _library.lookupFunction<_AddSerialN, _AddSerialD>('digitor_node_graph_add_serial_after'),
        _addParallel = _library.lookupFunction<_AddParallelN, _AddParallelD>('digitor_node_graph_add_parallel_after'),
        _remove = _library.lookupFunction<_NodeN, _NodeD>('digitor_node_graph_remove'),
        _connect = _library.lookupFunction<_ConnectN, _ConnectD>('digitor_node_graph_connect'),
        _disconnect = _library.lookupFunction<_ConnectN, _ConnectD>('digitor_node_graph_disconnect'),
        _position = _library.lookupFunction<_PositionN, _PositionD>('digitor_node_graph_set_position'),
        _enabled = _library.lookupFunction<_NodeBoolN, _NodeBoolD>('digitor_node_graph_set_enabled'),
        _bypassed = _library.lookupFunction<_NodeBoolN, _NodeBoolD>('digitor_node_graph_set_bypassed'),
        _clear = _library.lookupFunction<_NodeN, _NodeD>('digitor_node_graph_clear_operations'),
        _primary = _library.lookupFunction<_PrimaryN, _PrimaryD>('digitor_node_graph_add_primary_wheels'),
        _log = _library.lookupFunction<_LogN, _LogD>('digitor_node_graph_add_log_wheels'),
        _curves = _library.lookupFunction<_CurvesN, _CurvesD>('digitor_node_graph_add_rgb_curves'),
        _qualifier = _library.lookupFunction<_QualifierN, _QualifierD>('digitor_node_graph_add_hsl_qualifier'),
        _lut1d = _library.lookupFunction<_Lut1dN, _Lut1dD>('digitor_node_graph_add_lut1d'),
        _lut3d = _library.lookupFunction<_Lut3dN, _Lut3dD>('digitor_node_graph_add_lut3d'),
        _effect = _library.lookupFunction<_EffectN, _EffectD>('digitor_node_graph_add_effect'),
        _window = _library.lookupFunction<_WindowN, _WindowD>('digitor_node_graph_add_power_window'),
        _identity = _library.lookupFunction<_TextN, _TextD>('digitor_node_graph_recipe_identity'),
        _json = _library.lookupFunction<_TextN, _TextD>('digitor_node_graph_to_json');

  factory DigitorNativeNodeGraph.create({String? libraryPath}) {
    final library = DigitorLibraryLoader.open(overridePath: libraryPath);
    final create = library.lookupFunction<_GraphCreateN, _GraphCreateD>('digitor_node_graph_create');
    final out = calloc<Pointer<Void>>();
    try {
      _check('nodeGraphCreate', create(out));
      if (out.value == nullptr) throw const DigitorEngineException('nodeGraphCreate', 100);
      return DigitorNativeNodeGraph._(library, out.value);
    } finally {
      calloc.free(out);
    }
  }

  final DynamicLibrary _library;
  Pointer<Void> _handle;
  bool _disposed = false;
  final _GraphDestroyD _destroy;
  final _GetEndpointsD _getEndpoints;
  final _NodeD _select;
  final _AddSerialD _addSerial;
  final _AddParallelD _addParallel;
  final _NodeD _remove;
  final _ConnectD _connect;
  final _ConnectD _disconnect;
  final _PositionD _position;
  final _NodeBoolD _enabled;
  final _NodeBoolD _bypassed;
  final _NodeD _clear;
  final _PrimaryD _primary;
  final _LogD _log;
  final _CurvesD _curves;
  final _QualifierD _qualifier;
  final _Lut1dD _lut1d;
  final _Lut3dD _lut3d;
  final _EffectD _effect;
  final _WindowD _window;
  final _TextD _identity;
  final _TextD _json;

  ({DigitorNodeId input, DigitorNodeId output}) endpoints() {
    _ensureAlive();
    final input = calloc<Uint64>();
    final output = calloc<Uint64>();
    try {
      _check('nodeGraphEndpoints', _getEndpoints(_handle, input, output));
      return (input: input.value, output: output.value);
    } finally {
      calloc.free(input);
      calloc.free(output);
    }
  }

  void select(DigitorNodeId node) {
    _ensureAlive();
    _check('nodeGraphSelect', _select(_handle, node));
  }

  DigitorNodeId addSerialAfter(DigitorNodeId after, {String name = 'Serial Node'}) {
    _ensureAlive();
    final nativeName = name.toNativeUtf8();
    final out = calloc<Uint64>();
    try {
      _check('nodeGraphAddSerial', _addSerial(_handle, after, nativeName, out));
      return out.value;
    } finally {
      malloc.free(nativeName);
      calloc.free(out);
    }
  }

  ({DigitorNodeId first, DigitorNodeId second}) addParallelAfter(
    DigitorNodeId after, {
    String firstName = 'Parallel A',
    String secondName = 'Parallel B',
  }) {
    _ensureAlive();
    final firstNameNative = firstName.toNativeUtf8();
    final secondNameNative = secondName.toNativeUtf8();
    final first = calloc<Uint64>();
    final second = calloc<Uint64>();
    try {
      _check('nodeGraphAddParallel', _addParallel(_handle, after, firstNameNative, secondNameNative, first, second));
      return (first: first.value, second: second.value);
    } finally {
      malloc.free(firstNameNative);
      malloc.free(secondNameNative);
      calloc.free(first);
      calloc.free(second);
    }
  }

  void remove(DigitorNodeId node) {
    _ensureAlive();
    _check('nodeGraphRemove', _remove(_handle, node));
  }

  void connect(DigitorNodeId source, DigitorNodeId destination) {
    _ensureAlive();
    _check('nodeGraphConnect', _connect(_handle, source, destination));
  }

  void disconnect(DigitorNodeId source, DigitorNodeId destination) {
    _ensureAlive();
    _check('nodeGraphDisconnect', _disconnect(_handle, source, destination));
  }

  void setPosition(DigitorNodeId node, DigitorNodePosition position) {
    _ensureAlive();
    final native = calloc<_NativeNodePosition>();
    try {
      native.ref
        ..x = position.x
        ..y = position.y;
      _check('nodeGraphSetPosition', _position(_handle, node, native.ref));
    } finally {
      calloc.free(native);
    }
  }

  void setEnabled(DigitorNodeId node, bool enabled) {
    _ensureAlive();
    _check('nodeGraphSetEnabled', _enabled(_handle, node, enabled ? 1 : 0));
  }

  void setBypassed(DigitorNodeId node, bool bypassed) {
    _ensureAlive();
    _check('nodeGraphSetBypassed', _bypassed(_handle, node, bypassed ? 1 : 0));
  }

  void clearOperations(DigitorNodeId node) {
    _ensureAlive();
    _check('nodeGraphClearOperations', _clear(_handle, node));
  }

  void addPrimaryWheels(DigitorPrimaryWheels value) {
    _ensureAlive();
    final native = calloc<_NativePrimaryWheels>();
    try {
      _setRgb(native.ref.lift, value.lift.rgb);
      native.ref
        ..liftMaster = value.lift.master
        ..liftEnabled = value.lift.enabled ? 1 : 0;
      _setRgb(native.ref.gamma, value.gamma.rgb);
      native.ref
        ..gammaMaster = value.gamma.master
        ..gammaEnabled = value.gamma.enabled ? 1 : 0;
      _setRgb(native.ref.gain, value.gain.rgb);
      native.ref
        ..gainMaster = value.gain.master
        ..gainEnabled = value.gain.enabled ? 1 : 0;
      _setRgb(native.ref.offset, value.offset.rgb);
      native.ref
        ..offsetMaster = value.offset.master
        ..offsetEnabled = value.offset.enabled ? 1 : 0;
      _check('nodeGraphPrimaryWheels', _primary(_handle, native));
    } finally {
      calloc.free(native);
    }
  }

  void addLogWheels(DigitorLogWheels value) {
    _ensureAlive();
    final native = calloc<_NativeLogWheels>();
    try {
      _setLogWheel(native.ref.shadows, value.shadows);
      _setLogWheel(native.ref.midtones, value.midtones);
      _setLogWheel(native.ref.highlights, value.highlights);
      _setLogWheel(native.ref.global, value.global);
      native.ref
        ..shadowPivot = value.shadowPivot
        ..highlightPivot = value.highlightPivot
        ..transitionWidth = value.transitionWidth;
      _check('nodeGraphLogWheels', _log(_handle, native));
    } finally {
      calloc.free(native);
    }
  }

  void addRgbCurves(DigitorRgbCurves value) {
    _ensureAlive();
    final native = calloc<_NativeRgbCurves>();
    final allocations = <Pointer<_NativeCurvePoint>>[];
    try {
      _setCurve(native.ref.master, value.master, allocations);
      _setCurve(native.ref.red, value.red, allocations);
      _setCurve(native.ref.green, value.green, allocations);
      _setCurve(native.ref.blue, value.blue, allocations);
      native.ref.lutSize = value.lutSize;
      _check('nodeGraphRgbCurves', _curves(_handle, native));
    } finally {
      for (final pointer in allocations) calloc.free(pointer);
      calloc.free(native);
    }
  }

  void addHslQualifier(DigitorHslQualifier value) {
    _ensureAlive();
    final native = calloc<_NativeHslQualifier>();
    try {
      _setRange(native.ref.hue, value.hue);
      _setRange(native.ref.saturation, value.saturation);
      _setRange(native.ref.luminance, value.luminance);
      native.ref
        ..blur = value.blur
        ..denoise = value.denoise
        ..cleanBlack = value.cleanBlack
        ..cleanWhite = value.cleanWhite
        ..invert = value.invert ? 1 : 0
        ..matteOutput = value.matteOutput ? 1 : 0;
      _check('nodeGraphHslQualifier', _qualifier(_handle, native));
    } finally {
      calloc.free(native);
    }
  }

  void addLut1d(List<DigitorLutColor> values, {DigitorLutInterpolation interpolation = DigitorLutInterpolation.linear}) {
    if (values.length < 2) throw ArgumentError.value(values.length, 'values', 'A 1D LUT needs at least two values.');
    _ensureAlive();
    final colors = _allocateColors(values);
    final native = calloc<_NativeLut1d>();
    try {
      native.ref
        ..values = colors
        ..valueCount = values.length
        ..interpolation = interpolation.index;
      _check('nodeGraphLut1d', _lut1d(_handle, native));
    } finally {
      calloc.free(colors);
      calloc.free(native);
    }
  }

  void addLut3d(int size, List<DigitorLutColor> values, {DigitorLutInterpolation interpolation = DigitorLutInterpolation.tetrahedral}) {
    if (size < 2 || values.length != size * size * size) {
      throw ArgumentError('3D LUT value count must equal size³.');
    }
    _ensureAlive();
    final colors = _allocateColors(values);
    final native = calloc<_NativeLut3d>();
    try {
      native.ref
        ..size = size
        ..values = colors
        ..valueCount = values.length
        ..interpolation = interpolation.index;
      _check('nodeGraphLut3d', _lut3d(_handle, native));
    } finally {
      calloc.free(colors);
      calloc.free(native);
    }
  }

  void addEffect(DigitorNodeEffect value) {
    _ensureAlive();
    final native = calloc<_NativeEffect>();
    try {
      native.ref
        ..type = value.type.index
        ..amount = value.amount
        ..radius = value.radius
        ..angle = value.angle
        ..seed = value.seed;
      _check('nodeGraphEffect', _effect(_handle, native));
    } finally {
      calloc.free(native);
    }
  }

  void addPowerWindow(DigitorPowerWindow value) {
    _ensureAlive();
    final native = calloc<_NativePowerWindow>();
    try {
      native.ref
        ..shape = value.shape.index
        ..centerX = value.centerX
        ..centerY = value.centerY
        ..width = value.width
        ..height = value.height
        ..rotation = value.rotation
        ..feather = value.feather
        ..opacity = value.opacity
        ..invert = value.invert ? 1 : 0;
      _check('nodeGraphPowerWindow', _window(_handle, native));
    } finally {
      calloc.free(native);
    }
  }

  String recipeIdentity() => _readText(_identity, 'nodeGraphRecipeIdentity');
  String toJson() => _readText(_json, 'nodeGraphToJson');

  void dispose() {
    if (_disposed) return;
    _check('nodeGraphDestroy', _destroy(_handle));
    _handle = nullptr;
    _disposed = true;
  }

  String _readText(_TextD function, String operation) {
    _ensureAlive();
    final required = calloc<Uint64>();
    try {
      _check(operation, function(_handle, nullptr, 0, required));
      final buffer = calloc<Char>(required.value);
      try {
        _check(operation, function(_handle, buffer, required.value, required));
        return buffer.cast<Utf8>().toDartString();
      } finally {
        calloc.free(buffer);
      }
    } finally {
      calloc.free(required);
    }
  }

  static void _setRgb(_NativeRgb native, DigitorRgbValue value) {
    native
      ..r = value.r
      ..g = value.g
      ..b = value.b;
  }

  static void _setLogWheel(_NativeLogWheel native, DigitorLogWheelValue value) {
    _setRgb(native.rgb, value.rgb);
    native
      ..master = value.master
      ..enabled = value.enabled ? 1 : 0;
  }

  static void _setRange(_NativeQualifierRange native, DigitorQualifierRange value) {
    native
      ..low = value.low
      ..high = value.high
      ..softness = value.softness;
  }

  static void _setCurve(_NativeCurveChannel native, DigitorCurveChannel value, List<Pointer<_NativeCurvePoint>> allocations) {
    if (value.points.length < 2) throw ArgumentError('Every RGB curve channel needs at least two points.');
    final points = calloc<_NativeCurvePoint>(value.points.length);
    allocations.add(points);
    for (var index = 0; index < value.points.length; index++) {
      final source = value.points[index];
      (points + index).ref
        ..x = source.x
        ..y = source.y;
    }
    native
      ..points = points
      ..pointCount = value.points.length
      ..enabled = value.enabled ? 1 : 0;
  }

  static Pointer<_NativeLutColor> _allocateColors(List<DigitorLutColor> values) {
    final pointer = calloc<_NativeLutColor>(values.length);
    for (var index = 0; index < values.length; index++) {
      final source = values[index];
      (pointer + index).ref
        ..r = source.r
        ..g = source.g
        ..b = source.b
        ..a = source.a;
    }
    return pointer;
  }

  void _ensureAlive() {
    if (_disposed || _handle == nullptr) throw StateError('DigitorNativeNodeGraph is disposed.');
  }

  static void _check(String operation, int result) {
    if (result != 0) throw DigitorEngineException(operation, result);
  }
}
