@DefaultAsset('package:digitor_engine_ffi/digitor_engine_ffi.dart')
library;

import 'dart:ffi';

import 'package:ffi/ffi.dart';

final class DigitorNodeGraphNative extends Opaque {}

final class DigitorNodePositionNative extends Struct {
  @Float()
  external double x;
  @Float()
  external double y;
}

final class DigitorRgbNative extends Struct {
  @Float()
  external double r;
  @Float()
  external double g;
  @Float()
  external double b;
}

final class DigitorPrimaryWheelsControlsNative extends Struct {
  external DigitorRgbNative lift;
  @Float()
  external double liftMaster;
  @Uint8()
  external int liftEnabled;
  external DigitorRgbNative gamma;
  @Float()
  external double gammaMaster;
  @Uint8()
  external int gammaEnabled;
  external DigitorRgbNative gain;
  @Float()
  external double gainMaster;
  @Uint8()
  external int gainEnabled;
  external DigitorRgbNative offset;
  @Float()
  external double offsetMaster;
  @Uint8()
  external int offsetEnabled;
}

final class DigitorLogWheelControlNative extends Struct {
  external DigitorRgbNative rgb;
  @Float()
  external double master;
  @Uint8()
  external int enabled;
}

final class DigitorLogWheelsControlsNative extends Struct {
  external DigitorLogWheelControlNative shadows;
  external DigitorLogWheelControlNative midtones;
  external DigitorLogWheelControlNative highlights;
  external DigitorLogWheelControlNative global;
  @Float()
  external double shadowPivot;
  @Float()
  external double highlightPivot;
  @Float()
  external double transitionWidth;
}

final class DigitorCurvePointNative extends Struct {
  @Float()
  external double x;
  @Float()
  external double y;
}

final class DigitorCurveChannelNative extends Struct {
  external Pointer<DigitorCurvePointNative> points;
  @Uint32()
  external int pointCount;
  @Uint8()
  external int enabled;
}

final class DigitorRgbCurvesControlsNative extends Struct {
  external DigitorCurveChannelNative master;
  external DigitorCurveChannelNative red;
  external DigitorCurveChannelNative green;
  external DigitorCurveChannelNative blue;
  @Uint32()
  external int lutSize;
}

final class DigitorQualifierRangeNative extends Struct {
  @Float()
  external double low;
  @Float()
  external double high;
  @Float()
  external double softness;
}

final class DigitorHslQualifierControlsNative extends Struct {
  external DigitorQualifierRangeNative hue;
  external DigitorQualifierRangeNative saturation;
  external DigitorQualifierRangeNative luminance;
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

final class DigitorCorrectionControlsNative extends Struct {
  @Float()
  external double exposure;
  @Float()
  external double contrast;
  @Float()
  external double saturation;
  @Float()
  external double temperature;
  @Float()
  external double tint;
  @Float()
  external double highlights;
  @Float()
  external double shadows;
  @Float()
  external double hue;
  @Float()
  external double colorBoost;
}

final class DigitorLutColorNative extends Struct {
  @Float()
  external double r;
  @Float()
  external double g;
  @Float()
  external double b;
  @Float()
  external double a;
}

final class DigitorLut1DControlsNative extends Struct {
  external Pointer<DigitorLutColorNative> values;
  @Uint32()
  external int valueCount;
  @Int32()
  external int interpolation;
}

final class DigitorLut3DControlsNative extends Struct {
  @Uint32()
  external int size;
  external Pointer<DigitorLutColorNative> values;
  @Uint64()
  external int valueCount;
  @Int32()
  external int interpolation;
}

final class DigitorNodeEffectSettingsNative extends Struct {
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

final class DigitorPowerWindowSettingsNative extends Struct {
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

@Native<Int32 Function(Pointer<Pointer<DigitorNodeGraphNative>>)>(
  symbol: 'digitor_node_graph_create',
)
external int digitorNodeGraphCreate(
  Pointer<Pointer<DigitorNodeGraphNative>> outGraph,
);

@Native<Int32 Function(Pointer<DigitorNodeGraphNative>)>(
  symbol: 'digitor_node_graph_destroy',
)
external int digitorNodeGraphDestroy(Pointer<DigitorNodeGraphNative> graph);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Pointer<Uint64>,
    Pointer<Uint64>,
  )
>(symbol: 'digitor_node_graph_get_endpoints')
external int digitorNodeGraphGetEndpoints(
  Pointer<DigitorNodeGraphNative> graph,
  Pointer<Uint64> input,
  Pointer<Uint64> output,
);

@Native<Int32 Function(Pointer<DigitorNodeGraphNative>, Uint64)>(
  symbol: 'digitor_node_graph_select',
)
external int digitorNodeGraphSelect(
  Pointer<DigitorNodeGraphNative> graph,
  int node,
);

@Native<Int32 Function(Pointer<DigitorNodeGraphNative>, Pointer<Uint64>)>(
  symbol: 'digitor_node_graph_get_selected',
)
external int digitorNodeGraphGetSelected(
  Pointer<DigitorNodeGraphNative> graph,
  Pointer<Uint64> node,
);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Uint64,
    Pointer<Utf8>,
    Pointer<Uint64>,
  )
>(symbol: 'digitor_node_graph_add_serial_after')
external int digitorNodeGraphAddSerialAfter(
  Pointer<DigitorNodeGraphNative> graph,
  int after,
  Pointer<Utf8> name,
  Pointer<Uint64> outNode,
);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Uint64,
    Pointer<Utf8>,
    Pointer<Utf8>,
    Pointer<Uint64>,
    Pointer<Uint64>,
  )
>(symbol: 'digitor_node_graph_add_parallel_after')
external int digitorNodeGraphAddParallelAfter(
  Pointer<DigitorNodeGraphNative> graph,
  int after,
  Pointer<Utf8> firstName,
  Pointer<Utf8> secondName,
  Pointer<Uint64> first,
  Pointer<Uint64> second,
);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Uint64,
    Pointer<Utf8>,
    Pointer<Uint64>,
  )
>(symbol: 'digitor_node_graph_convert_to_parallel')
external int digitorNodeGraphConvertToParallel(
  Pointer<DigitorNodeGraphNative> graph,
  int existing,
  Pointer<Utf8> branchName,
  Pointer<Uint64> branch,
);

@Native<Int32 Function(Pointer<DigitorNodeGraphNative>, Uint64)>(
  symbol: 'digitor_node_graph_remove',
)
external int digitorNodeGraphRemove(
  Pointer<DigitorNodeGraphNative> graph,
  int node,
);

@Native<Int32 Function(Pointer<DigitorNodeGraphNative>, Uint64, Uint64)>(
  symbol: 'digitor_node_graph_connect',
)
external int digitorNodeGraphConnect(
  Pointer<DigitorNodeGraphNative> graph,
  int source,
  int destination,
);

@Native<Int32 Function(Pointer<DigitorNodeGraphNative>, Uint64, Uint64)>(
  symbol: 'digitor_node_graph_disconnect',
)
external int digitorNodeGraphDisconnect(
  Pointer<DigitorNodeGraphNative> graph,
  int source,
  int destination,
);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Uint64,
    DigitorNodePositionNative,
  )
>(symbol: 'digitor_node_graph_set_position')
external int digitorNodeGraphSetPosition(
  Pointer<DigitorNodeGraphNative> graph,
  int node,
  DigitorNodePositionNative position,
);

@Native<Int32 Function(Pointer<DigitorNodeGraphNative>, Uint64, Uint8)>(
  symbol: 'digitor_node_graph_set_enabled',
)
external int digitorNodeGraphSetEnabled(
  Pointer<DigitorNodeGraphNative> graph,
  int node,
  int enabled,
);

@Native<Int32 Function(Pointer<DigitorNodeGraphNative>, Uint64, Uint8)>(
  symbol: 'digitor_node_graph_set_bypassed',
)
external int digitorNodeGraphSetBypassed(
  Pointer<DigitorNodeGraphNative> graph,
  int node,
  int bypassed,
);

@Native<Int32 Function(Pointer<DigitorNodeGraphNative>, Uint64)>(
  symbol: 'digitor_node_graph_clear_operations',
)
external int digitorNodeGraphClearOperations(
  Pointer<DigitorNodeGraphNative> graph,
  int node,
);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Pointer<DigitorPrimaryWheelsControlsNative>,
  )
>(symbol: 'digitor_node_graph_add_primary_wheels')
external int digitorNodeGraphAddPrimaryWheels(
  Pointer<DigitorNodeGraphNative> graph,
  Pointer<DigitorPrimaryWheelsControlsNative> controls,
);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Pointer<DigitorLogWheelsControlsNative>,
  )
>(symbol: 'digitor_node_graph_add_log_wheels')
external int digitorNodeGraphAddLogWheels(
  Pointer<DigitorNodeGraphNative> graph,
  Pointer<DigitorLogWheelsControlsNative> controls,
);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Pointer<DigitorRgbCurvesControlsNative>,
  )
>(symbol: 'digitor_node_graph_add_rgb_curves')
external int digitorNodeGraphAddRgbCurves(
  Pointer<DigitorNodeGraphNative> graph,
  Pointer<DigitorRgbCurvesControlsNative> controls,
);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Pointer<DigitorHslQualifierControlsNative>,
  )
>(symbol: 'digitor_node_graph_add_hsl_qualifier')
external int digitorNodeGraphAddHslQualifier(
  Pointer<DigitorNodeGraphNative> graph,
  Pointer<DigitorHslQualifierControlsNative> controls,
);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Pointer<DigitorCorrectionControlsNative>,
  )
>(symbol: 'digitor_node_graph_add_correction')
external int digitorNodeGraphAddCorrection(
  Pointer<DigitorNodeGraphNative> graph,
  Pointer<DigitorCorrectionControlsNative> controls,
);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Pointer<DigitorLut1DControlsNative>,
  )
>(symbol: 'digitor_node_graph_add_lut1d')
external int digitorNodeGraphAddLut1d(
  Pointer<DigitorNodeGraphNative> graph,
  Pointer<DigitorLut1DControlsNative> controls,
);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Pointer<DigitorLut3DControlsNative>,
  )
>(symbol: 'digitor_node_graph_add_lut3d')
external int digitorNodeGraphAddLut3d(
  Pointer<DigitorNodeGraphNative> graph,
  Pointer<DigitorLut3DControlsNative> controls,
);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Pointer<DigitorNodeEffectSettingsNative>,
  )
>(symbol: 'digitor_node_graph_add_effect')
external int digitorNodeGraphAddEffect(
  Pointer<DigitorNodeGraphNative> graph,
  Pointer<DigitorNodeEffectSettingsNative> settings,
);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Pointer<DigitorPowerWindowSettingsNative>,
  )
>(symbol: 'digitor_node_graph_add_power_window')
external int digitorNodeGraphAddPowerWindow(
  Pointer<DigitorNodeGraphNative> graph,
  Pointer<DigitorPowerWindowSettingsNative> settings,
);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Pointer<Uint8>,
    Uint64,
    Pointer<Uint64>,
  )
>(symbol: 'digitor_node_graph_recipe_identity')
external int digitorNodeGraphRecipeIdentity(
  Pointer<DigitorNodeGraphNative> graph,
  Pointer<Uint8> buffer,
  int bufferSize,
  Pointer<Uint64> required,
);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Pointer<Uint8>,
    Uint64,
    Pointer<Uint64>,
  )
>(symbol: 'digitor_node_graph_to_json')
external int digitorNodeGraphToJson(
  Pointer<DigitorNodeGraphNative> graph,
  Pointer<Uint8> buffer,
  int bufferSize,
  Pointer<Uint64> required,
);
